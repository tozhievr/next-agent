#pragma once

#include "engine/atn/atn.h"
#include "engine/lex/state_interner.h"
#include "engine/uast/uast.h"
#include "host/limits.hpp"
#include "host/metrics.hpp"
#include "lx/lx_language.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lx::host {

struct LanguageManifest {
  std::string id;
  std::string display_name;
  std::vector<std::string> extensions;  // normalized lowercase with leading '.'
  uint32_t abi_version{LX_ABI_VERSION};
  uint64_t grammar_hash{0};
  uint32_t lang_version{0};
  std::filesystem::path manifest_path;
  std::filesystem::path library_path;
  std::filesystem::path queries_dir;
  std::string highlight_query;
  std::string fold_query;
  std::string outline_query;
  std::string inject_query;
  // Keys are decimal ABI symbol IDs, avoiding case-folding in manifest tools.
  std::unordered_map<std::string, std::vector<std::string>> token_literals;
};

struct LoadedModule;

/**
 * Callback type for optional ATN binary validation (wired to engine/atn when present).
 * Returns true if valid, false with error_out filled otherwise.
 */
using AtnValidateFn = std::function<bool(const lx_parser_tables* tables, std::string* error_out)>;

/**
 * Reference-counted handle to a loaded language module (DLL or static test module).
 * Calls shutdown() and FreeLibrary() only when the last reference (registry + all
 * sessions + all snapshots) is released (§10.3).
 */
struct LoadedModule {
  std::string id;
  uint16_t lang_idx{0};
  uint64_t module_epoch{0};
  void* native_handle{nullptr};  // HMODULE on Windows, nullptr for static modules
  const lx_language_v1* vtable{nullptr};
  const lx_language_info* info{nullptr};
  std::shared_ptr<const lx::atn::Atn> atn;
  LanguageManifest manifest;

  // Derived lookup tables built at load time for O(1) queries
  std::unordered_map<std::string, lx_symbol> symbol_by_name;
  std::unordered_map<std::string, lx_field> field_by_name;
  // supertype_map[sub_sym] = list of super_syms (transitive closure or direct)
  std::unordered_map<lx_symbol, std::vector<lx_symbol>> supertypes_of;

  mutable std::atomic<uint32_t> crash_count{0};
  mutable std::atomic<bool> disabled{false};

  // Optional DFA cache or engine-owned state attached to this module epoch
  mutable std::mutex cache_mu;
  mutable std::shared_ptr<void> parser_cache;

  HostMetrics* metrics{nullptr};
  TraceLog* trace{nullptr};
  uint32_t max_crashes{3};

  ~LoadedModule();

  LoadedModule(const LoadedModule&) = delete;
  LoadedModule& operator=(const LoadedModule&) = delete;
  LoadedModule() = default;

  std::string_view symbol_name(lx_symbol sym) const noexcept;
  uint32_t symbol_flags(lx_symbol sym) const noexcept;
  std::string_view field_name(lx_field field) const noexcept;
  bool is_subtype_of(lx_symbol sub, lx_symbol super) const noexcept;

  // Crash-isolated ABI calls (§7.1, §14)
  lx_status safe_lex(const lx_lex_request* req, lx_lex_result* res) const noexcept;
  int safe_eval_pred(uint32_t pred_id, const lx_pred_view* view, bool* out_ok = nullptr) const noexcept;
  const void* safe_get_extension(std::string_view name) const noexcept;

  void record_crash(std::string_view where, uint32_t seh_code, std::string_view cpp_err) const;
};

struct LoadError {
  std::string message;
};

/**
 * Thread-safe language registry & lazy DLL loader (§7.2, §10.3, §14).
 */
class LanguageRegistry {
 public:
  explicit LanguageRegistry(HostConfig config = {},
                            HostMetrics* metrics = nullptr,
                            TraceLog* trace = nullptr);
  ~LanguageRegistry();

  LanguageRegistry(const LanguageRegistry&) = delete;
  LanguageRegistry& operator=(const LanguageRegistry&) = delete;

  void set_atn_validator(AtnValidateFn fn);

  lx::StateInterner& state_interner() noexcept { return state_interner_; }
  const lx::StateInterner& state_interner() const noexcept { return state_interner_; }
  lx::uast::TokenInterner& token_interner() noexcept { return token_interner_; }
  const lx::uast::TokenInterner& token_interner() const noexcept { return token_interner_; }
  const lx_host_api* host_api() const noexcept { return &host_api_; }

  /**
   * Indexes a single *.lxmanifest.json file without loading the DLL.
   */
  bool register_manifest(const std::filesystem::path& manifest_path, std::string* out_error = nullptr);

  /**
   * Scans `dir` for *.lxmanifest.json files and indexes them lazily (§7.2).
   * Returns count of manifests successfully indexed.
   */
  size_t index_directory(const std::filesystem::path& dir, bool recursive = true);

  /**
   * Registers an already-built manifest entry.
   */
  void register_manifest_entry(LanguageManifest manifest);

  /**
   * Registers an in-process static lx_language_v1 module (useful for tests).
   */
  std::shared_ptr<const LoadedModule> register_static_module(
      const lx_language_v1* vtable,
      std::string* out_error = nullptr,
      LanguageManifest manifest_override = {});

  /**
   * Lazily loads (via LoadLibraryEx) and returns the module for `lang_id`.
   */
  std::shared_ptr<const LoadedModule> get_or_load(std::string_view lang_id,
                                                  std::string* out_error = nullptr);

  /**
   * Resolves language by file extension (e.g. ".json") or URI/path and lazily loads it.
   */
  std::shared_ptr<const LoadedModule> get_or_load_by_extension(std::string_view ext,
                                                               std::string* out_error = nullptr);
  std::shared_ptr<const LoadedModule> get_or_load_for_uri(std::string_view uri_or_path,
                                                          std::string* out_error = nullptr);

  /**
   * Returns an already-loaded module without triggering lazy LoadLibraryEx.
   */
  std::shared_ptr<const LoadedModule> find_loaded(std::string_view lang_id) const;
  std::shared_ptr<const LoadedModule> find_loaded_by_lang_idx(uint16_t lang_idx) const;

  std::optional<std::string> resolve_language_id_by_extension(std::string_view ext) const;
  bool is_registered(std::string_view lang_id) const;
  bool is_disabled(std::string_view lang_id) const;
  void reset_disabled(std::string_view lang_id);

  /**
   * Hot-reloads a language from a new DLL or manifest under a fresh module_epoch (§10.3).
   * Drops registry's reference to the old LoadedModule; FreeLibrary occurs once all
   * existing snapshots/sessions release it.
   */
  std::shared_ptr<const LoadedModule> reload_language(
      std::string_view lang_id,
      const std::filesystem::path& new_library_or_manifest = {},
      std::string* out_error = nullptr);

  /**
   * Drops registry's reference to the loaded module (FreeLibrary happens when all
   * snapshots and sessions holding it are destroyed).
   */
  void unload_language(std::string_view lang_id);

  std::vector<std::string> registered_languages() const;
  uint64_t current_epoch() const noexcept { return epoch_counter_.load(std::memory_order_relaxed); }

 private:
  static void* LX_CALL api_alloc(size_t n);
  static void LX_CALL api_free(void* p);
  static void LX_CALL api_log(int level, lx_str msg);
  static uint64_t LX_CALL api_state_intern(lx_str blob);
  static lx_str LX_CALL api_state_resolve(uint64_t id);

  struct Entry {
    LanguageManifest manifest;
    uint16_t lang_idx{0};
    std::shared_ptr<const LoadedModule> loaded;
    bool disabled{false};
    std::string last_error;
  };

  std::shared_ptr<const LoadedModule> load_entry_locked(Entry& entry, std::string* out_error);
  std::shared_ptr<const LoadedModule> build_module(const lx_language_v1* vtable,
                                                   void* native_handle,
                                                   uint16_t lang_idx,
                                                   const LanguageManifest* manifest,
                                                   std::string* out_error);
  uint16_t alloc_lang_idx_locked();

  static std::string normalize_ext(std::string_view ext);

  HostConfig config_;
  HostMetrics* metrics_{nullptr};
  TraceLog* trace_{nullptr};
  lx::StateInterner state_interner_;
  lx::uast::TokenInterner token_interner_;
  lx_host_api host_api_{};
  AtnValidateFn atn_validator_;

  mutable std::shared_mutex mu_;
  std::mutex load_mu_;
  std::unordered_map<std::string, Entry> entries_;
  std::unordered_map<std::string, std::string> ext_to_id_;
  std::unordered_map<uint16_t, std::weak_ptr<const LoadedModule>> by_lang_idx_;
  uint16_t next_lang_idx_{1};
  std::atomic<uint64_t> epoch_counter_{1};
};

}  // namespace lx::host
