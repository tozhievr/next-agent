#include "host/module_loader.hpp"

#include "host/json.hpp"
#include "host/seh_guard.h"
#include "host/uri.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace lx::host {

namespace {

std::atomic<LanguageRegistry*> g_active_registry{nullptr};

lx::StateInterner& fallback_interner() {
  static lx::StateInterner s;
  return s;
}

std::string read_file_text(const std::filesystem::path& p, std::string* out_err) {
  std::ifstream f(p, std::ios::binary);
  if (!f) {
    if (out_err) *out_err = "cannot open file: " + p.string();
    return {};
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string_view to_sv(lx_str s) noexcept {
  if (!s.ptr || s.len == 0) return {};
  return std::string_view(s.ptr, s.len);
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  size_t off = s.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[off + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

LoadedModule::~LoadedModule() {
  if (vtable && vtable->shutdown) {
    (void)guarded_call([&]() { vtable->shutdown(); });
  }
#if defined(_WIN32)
  if (native_handle) {
    FreeLibrary(static_cast<HMODULE>(native_handle));
    native_handle = nullptr;
  }
#else
  if (native_handle) {
    dlclose(native_handle);
    native_handle = nullptr;
  }
#endif
}

std::string_view LoadedModule::symbol_name(lx_symbol sym) const noexcept {
  if (!info || !info->symbols || sym >= info->symbol_count) return {};
  return to_sv(info->symbols[sym].name);
}

uint32_t LoadedModule::symbol_flags(lx_symbol sym) const noexcept {
  if (!info || !info->symbols || sym >= info->symbol_count) return 0;
  return info->symbols[sym].flags;
}

std::string_view LoadedModule::field_name(lx_field field) const noexcept {
  if (!info || !info->fields || field >= info->field_count) return {};
  return to_sv(info->fields[field]);
}

bool LoadedModule::is_subtype_of(lx_symbol sub, lx_symbol super) const noexcept {
  if (sub == super) return true;
  auto it = supertypes_of.find(sub);
  if (it == supertypes_of.end()) return false;
  for (lx_symbol s : it->second) {
    if (s == super) return true;
  }
  return false;
}

void LoadedModule::record_crash(std::string_view where,
                                uint32_t seh_code,
                                std::string_view cpp_err) const {
  if (metrics) {
    metrics->module_crashes.fetch_add(1, std::memory_order_relaxed);
  }
  uint32_t count = crash_count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count >= max_crashes) {
    disabled.store(true, std::memory_order_release);
  }
  if (trace) {
    std::ostringstream msg;
    msg << "module '" << id << "' crashed in " << where;
    if (seh_code != 0) {
      msg << " (SEH 0x" << std::hex << seh_code << std::dec << ")";
    }
    if (!cpp_err.empty()) {
      msg << " (" << cpp_err << ")";
    }
    msg << " [count=" << count << "/" << max_crashes << "]";
    trace->log(LogLevel::Error, "module", {}, msg.str());
  }
}

lx_status LoadedModule::safe_lex(const lx_lex_request* req, lx_lex_result* res) const noexcept {
  if (disabled.load(std::memory_order_acquire) || !vtable || !vtable->lex) {
    return LX_BAD_STATE;
  }
  lx_status status = LX_INTERNAL;
  auto g = guarded_call([&]() { status = vtable->lex(req, res); });
  if (!g.ok) {
    record_crash("lex", g.seh_code, g.cpp_error);
    return LX_INTERNAL;
  }
  return status;
}

int LoadedModule::safe_eval_pred(uint32_t pred_id, const lx_pred_view* view, bool* out_ok) const noexcept {
  if (disabled.load(std::memory_order_acquire) || !vtable || !vtable->eval_pred) {
    if (out_ok) *out_ok = false;
    return 0;
  }
  int result = 0;
  auto g = guarded_call([&]() { result = vtable->eval_pred(pred_id, view); });
  if (!g.ok) {
    record_crash("eval_pred", g.seh_code, g.cpp_error);
    if (out_ok) *out_ok = false;
    return 0;
  }
  if (out_ok) *out_ok = true;
  return result;
}

const void* LoadedModule::safe_get_extension(std::string_view name) const noexcept {
  if (disabled.load(std::memory_order_acquire) || !vtable || !vtable->get_extension) {
    return nullptr;
  }
  const void* ext = nullptr;
  lx_str s{name.data(), static_cast<uint32_t>(name.size())};
  auto g = guarded_call([&]() { ext = vtable->get_extension(s); });
  if (!g.ok) {
    record_crash("get_extension", g.seh_code, g.cpp_error);
    return nullptr;
  }
  return ext;
}

// ============================================================================
// LanguageRegistry
// ============================================================================

LanguageRegistry::LanguageRegistry(HostConfig config, HostMetrics* metrics, TraceLog* trace)
    : config_(config), metrics_(metrics), trace_(trace) {
  host_api_.size = sizeof(lx_host_api);
  host_api_.abi_version = LX_ABI_VERSION;
  host_api_.alloc = &LanguageRegistry::api_alloc;
  host_api_.free = &LanguageRegistry::api_free;
  host_api_.log = &LanguageRegistry::api_log;
  host_api_.state_intern = &LanguageRegistry::api_state_intern;
  host_api_.state_resolve = &LanguageRegistry::api_state_resolve;

  LanguageRegistry* expected = nullptr;
  g_active_registry.compare_exchange_strong(expected, this, std::memory_order_acq_rel);
}

LanguageRegistry::~LanguageRegistry() {
  {
    std::unique_lock lock(mu_);
    entries_.clear();
    ext_to_id_.clear();
    by_lang_idx_.clear();
  }
  LanguageRegistry* expected = this;
  g_active_registry.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

void LanguageRegistry::set_atn_validator(AtnValidateFn fn) {
  std::lock_guard lock(load_mu_);
  atn_validator_ = std::move(fn);
}

void* LX_CALL LanguageRegistry::api_alloc(size_t n) {
  if (auto* reg = g_active_registry.load(std::memory_order_acquire)) {
    if (reg->metrics_) {
      reg->metrics_->alloc_count.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return std::malloc(n > 0 ? n : 1);
}

void LX_CALL LanguageRegistry::api_free(void* p) {
  std::free(p);
}

void LX_CALL LanguageRegistry::api_log(int level, lx_str msg) {
  if (auto* reg = g_active_registry.load(std::memory_order_acquire)) {
    if (reg->trace_) {
      LogLevel lvl = LogLevel::Info;
      if (level <= 0) lvl = LogLevel::Error;
      else if (level == 1) lvl = LogLevel::Warn;
      else if (level == 2) lvl = LogLevel::Info;
      else if (level == 3) lvl = LogLevel::Debug;
      else lvl = LogLevel::Trace;
      reg->trace_->log(lvl, "plugin", {}, std::string(to_sv(msg)));
    }
  }
}

uint64_t LX_CALL LanguageRegistry::api_state_intern(lx_str blob) {
  if (auto* reg = g_active_registry.load(std::memory_order_acquire)) {
    return reg->state_interner_.intern_ext(blob.ptr, blob.len);
  }
  return fallback_interner().intern_ext(blob.ptr, blob.len);
}

lx_str LX_CALL LanguageRegistry::api_state_resolve(uint64_t id) {
  std::string_view sv;
  if (auto* reg = g_active_registry.load(std::memory_order_acquire)) {
    sv = reg->state_interner_.resolve_ext(id);
  } else {
    sv = fallback_interner().resolve_ext(id);
  }
  return lx_str{sv.data(), static_cast<uint32_t>(sv.size())};
}

std::string LanguageRegistry::normalize_ext(std::string_view ext) {
  std::string out;
  out.reserve(ext.size() + 1);
  if (!ext.empty() && ext.front() != '.') {
    out.push_back('.');
  }
  for (char c : ext) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool LanguageRegistry::register_manifest(const std::filesystem::path& manifest_path,
                                         std::string* out_error) {
  std::string err;
  std::string text = read_file_text(manifest_path, &err);
  if (text.empty() && !err.empty()) {
    if (out_error) *out_error = err;
    return false;
  }
  auto parsed = parse_json(text);
  if (!parsed || !parsed.value->is_object()) {
    if (out_error) {
      *out_error = "invalid manifest JSON in " + manifest_path.string() + ": " + parsed.error;
    }
    return false;
  }
  const auto& obj = *parsed.value;
  std::string id(obj.at("id").as_string());
  if (id.empty()) {
    if (out_error) *out_error = "manifest missing 'id': " + manifest_path.string();
    return false;
  }

  LanguageManifest m;
  m.id = std::move(id);
  m.display_name = std::string(obj.at("display_name").as_string());
  if (m.display_name.empty()) m.display_name = m.id;
  m.abi_version = static_cast<uint32_t>(obj.at("abi_version").as_int(LX_ABI_VERSION));
  m.lang_version = static_cast<uint32_t>(obj.at("lang_version").as_int(0));
  if (const auto* gh = obj.find("grammar_hash")) {
    auto u = json_to_u64(*gh);
    if (!u) {
      if (out_error) *out_error = "invalid grammar_hash in manifest: " + manifest_path.string();
      return false;
    }
    m.grammar_hash = *u;
  }
  for (const auto& ext_v : obj.at("extensions").as_array()) {
    if (ext_v.is_string() && !ext_v.as_string().empty()) {
      m.extensions.push_back(normalize_ext(ext_v.as_string()));
    }
  }

  if (const auto* literals = obj.find("token_literals"); literals && literals->is_object()) {
    for (const auto& [name, values] : literals->as_object()) {
      for (const auto& value : values.as_array()) {
        if (value.is_string() && !value.as_string().empty()) {
          m.token_literals[name].emplace_back(value.as_string());
        }
      }
    }
  }

  m.manifest_path = manifest_path;
  auto dir = manifest_path.parent_path();
  std::string lib_str(obj.at("library").as_string());
  if (lib_str.empty()) lib_str = std::string(obj.at("dll").as_string());
  if (lib_str.empty()) lib_str = std::string(obj.at("module").as_string());
  if (!lib_str.empty()) {
    std::filesystem::path lp(lib_str);
    m.library_path = lp.is_absolute() ? lp : (dir / lp);
  } else {
    // Strip ".lxmanifest.json" -> "<stem>.dll"
    std::string fname = manifest_path.filename().string();
    constexpr std::string_view kSuffix = ".lxmanifest.json";
    std::string stem = ends_with_ci(fname, kSuffix)
                           ? fname.substr(0, fname.size() - kSuffix.size())
                           : manifest_path.stem().string();
#if defined(_WIN32)
    m.library_path = dir / (stem + ".dll");
#else
    m.library_path = dir / ("lib" + stem + ".so");
#endif
  }

  // Optional query directory / files (§8.4)
  if (const auto* qd = obj.find("queries_dir"); qd && qd->is_string()) {
    std::filesystem::path p(qd->as_string());
    m.queries_dir = p.is_absolute() ? p : (dir / p);
  } else {
    auto candidate = dir / (m.id + ".queries");
    if (std::filesystem::exists(candidate)) {
      m.queries_dir = candidate;
    }
  }
  auto load_query_opt = [&](const char* key, const char* default_file) -> std::string {
    if (const auto* qobj = obj.find("queries"); qobj && qobj->is_object()) {
      if (const auto* f = qobj->find(key); f && f->is_string()) {
        std::filesystem::path p(f->as_string());
        if (!p.is_absolute()) p = dir / p;
        return read_file_text(p, nullptr);
      }
    }
    if (!m.queries_dir.empty()) {
      auto p = m.queries_dir / default_file;
      if (std::filesystem::exists(p)) {
        return read_file_text(p, nullptr);
      }
    }
    return {};
  };
  m.highlight_query = load_query_opt("highlight", "highlights.scm");
  m.fold_query = load_query_opt("fold", "folds.scm");
  m.outline_query = load_query_opt("outline", "outline.scm");
  m.inject_query = load_query_opt("inject", "injections.scm");

  register_manifest_entry(std::move(m));
  return true;
}

size_t LanguageRegistry::index_directory(const std::filesystem::path& dir, bool recursive) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
    return 0;
  }
  size_t count = 0;
  auto process_entry = [&](const std::filesystem::directory_entry& de) {
    if (!de.is_regular_file(ec)) return;
    std::string fname = de.path().filename().string();
    if (ends_with_ci(fname, ".lxmanifest.json")) {
      std::string err;
      if (register_manifest(de.path(), &err)) {
        ++count;
      } else if (trace_) {
        trace_->log(LogLevel::Warn, "module", {}, err);
      }
    }
  };

  if (recursive) {
    for (const auto& de : std::filesystem::recursive_directory_iterator(dir, ec)) {
      process_entry(de);
    }
  } else {
    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
      process_entry(de);
    }
  }
  return count;
}

void LanguageRegistry::register_manifest_entry(LanguageManifest manifest) {
  std::unique_lock lock(mu_);
  std::string id = manifest.id;
  for (const auto& ext : manifest.extensions) {
    ext_to_id_[normalize_ext(ext)] = id;
  }
  auto& entry = entries_[id];
  entry.manifest = std::move(manifest);
  if (entry.lang_idx == 0) {
    entry.lang_idx = alloc_lang_idx_locked();
  }
  if (trace_) {
    trace_->log(LogLevel::Info, "module", {},
                "indexed manifest for '" + id + "' -> " + entry.manifest.library_path.string());
  }
}

uint16_t LanguageRegistry::alloc_lang_idx_locked() {
  uint16_t idx = next_lang_idx_++;
  if (next_lang_idx_ == 0) next_lang_idx_ = 1;
  return idx;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::build_module(
    const lx_language_v1* vtable,
    void* native_handle,
    uint16_t lang_idx,
    const LanguageManifest* manifest,
    std::string* out_error) {
  auto cleanup_handle = [&]() {
#if defined(_WIN32)
    if (native_handle) {
      FreeLibrary(static_cast<HMODULE>(native_handle));
      native_handle = nullptr;
    }
#else
    if (native_handle) {
      dlclose(native_handle);
      native_handle = nullptr;
    }
#endif
  };

  if (!vtable) {
    if (out_error) *out_error = "lx_get_language returned null";
    cleanup_handle();
    return nullptr;
  }
  constexpr size_t kMinVtableSize = offsetof(lx_language_v1, shutdown) + sizeof(void*);
  if (vtable->size < kMinVtableSize) {
    if (out_error) *out_error = "module lx_language_v1.size too small";
    cleanup_handle();
    return nullptr;
  }
  if (vtable->abi_version != LX_ABI_VERSION) {
    if (out_error) {
      *out_error = "incompatible ABI version: got " + std::to_string(vtable->abi_version) +
                   ", expected " + std::to_string(LX_ABI_VERSION);
    }
    cleanup_handle();
    return nullptr;
  }
  if (!vtable->info || !vtable->lex) {
    if (out_error) *out_error = "module missing required info() or lex() function";
    cleanup_handle();
    return nullptr;
  }

  const lx_language_info* info = nullptr;
  auto g_info = guarded_call([&]() { info = vtable->info(); });
  if (!g_info.ok || !info) {
    if (out_error) *out_error = "module info() failed or trapped";
    cleanup_handle();
    return nullptr;
  }
  constexpr size_t kMinInfoSize = offsetof(lx_language_info, capabilities) + sizeof(uint32_t);
  if (info->size < kMinInfoSize) {
    if (out_error) *out_error = "module lx_language_info.size too small";
    cleanup_handle();
    return nullptr;
  }

  std::string mod_id(to_sv(info->id));
  if (mod_id.empty()) {
    if (out_error) *out_error = "module reported empty language id";
    cleanup_handle();
    return nullptr;
  }

  if (manifest) {
    if (!manifest->id.empty() && mod_id != manifest->id) {
      if (out_error) {
        *out_error = "manifest id '" + manifest->id + "' mismatches module id '" + mod_id + "'";
      }
      cleanup_handle();
      return nullptr;
    }
    if (manifest->grammar_hash != 0 && info->grammar_hash != manifest->grammar_hash) {
      if (out_error) {
        *out_error = "grammar_hash mismatch between manifest and module for '" + mod_id + "'";
      }
      cleanup_handle();
      return nullptr;
    }
    if (manifest->lang_version != 0 && info->lang_version != manifest->lang_version) {
      if (out_error) {
        *out_error = "lang_version mismatch between manifest and module for '" + mod_id + "'";
      }
      cleanup_handle();
      return nullptr;
    }
  }

  std::shared_ptr<const lx::atn::Atn> loaded_atn;
  if (vtable->parser) {
    if (vtable->parser->size < offsetof(lx_parser_tables, pred_names) + sizeof(void*)) {
      if (out_error) *out_error = "module lx_parser_tables.size too small";
      cleanup_handle();
      return nullptr;
    }
    if (vtable->parser->atn && vtable->parser->atn_size > 0) {
      std::string atn_err;
      auto atn_opt = lx::atn::Atn::load(*vtable->parser, &atn_err);
      if (!atn_opt) {
        if (out_error) *out_error = "ATN load/validation failed for '" + mod_id + "': " + atn_err;
        cleanup_handle();
        return nullptr;
      }
      if (!atn_opt->validate_against_info(*info, *vtable->parser, &atn_err)) {
        if (out_error) *out_error = "ATN info cross-check failed for '" + mod_id + "': " + atn_err;
        cleanup_handle();
        return nullptr;
      }
      loaded_atn = std::make_shared<const lx::atn::Atn>(std::move(*atn_opt));
    }
    if (atn_validator_) {
      std::string atn_err;
      if (!atn_validator_(vtable->parser, &atn_err)) {
        if (out_error) *out_error = "ATN validation failed for '" + mod_id + "': " + atn_err;
        cleanup_handle();
        return nullptr;
      }
    }
  }

  auto mod = std::make_shared<LoadedModule>();
  mod->id = mod_id;
  mod->lang_idx = lang_idx;
  mod->module_epoch = epoch_counter_.fetch_add(1, std::memory_order_relaxed);
  mod->native_handle = native_handle;
  mod->vtable = vtable;
  mod->info = info;
  mod->atn = std::move(loaded_atn);
  if (manifest) {
    mod->manifest = *manifest;
  } else {
    mod->manifest.id = mod_id;
    mod->manifest.display_name = std::string(to_sv(info->display_name));
    mod->manifest.abi_version = vtable->abi_version;
    mod->manifest.grammar_hash = info->grammar_hash;
    mod->manifest.lang_version = info->lang_version;
    if (info->extensions && info->extension_count > 0) {
      for (uint32_t i = 0; i < info->extension_count; ++i) {
        auto ext = to_sv(info->extensions[i]);
        if (!ext.empty()) mod->manifest.extensions.push_back(normalize_ext(ext));
      }
    }
  }
  mod->metrics = metrics_;
  mod->trace = trace_;
  mod->max_crashes = config_.max_module_crashes;

  if (info->symbols && info->symbol_count > 0) {
    for (uint32_t i = 0; i < info->symbol_count; ++i) {
      auto name = to_sv(info->symbols[i].name);
      if (!name.empty()) {
        mod->symbol_by_name.emplace(std::string(name), static_cast<lx_symbol>(i));
      }
    }
  }
  if (info->fields && info->field_count > 0) {
    for (uint32_t i = 0; i < info->field_count; ++i) {
      auto name = to_sv(info->fields[i]);
      if (!name.empty()) {
        mod->field_by_name.emplace(std::string(name), static_cast<lx_field>(i));
      }
    }
  }
  if (info->supertypes && info->supertype_count > 0) {
    for (uint32_t i = 0; i < info->supertype_count; ++i) {
      const auto& e = info->supertypes[i];
      mod->supertypes_of[e.sub].push_back(e.super);
    }
    // Transitive closure (bounded for safety)
    bool changed = true;
    for (uint32_t pass = 0; pass < 16 && changed; ++pass) {
      changed = false;
      for (auto& [sub, supers] : mod->supertypes_of) {
        std::vector<lx_symbol> add;
        for (lx_symbol s : supers) {
          auto it = mod->supertypes_of.find(s);
          if (it != mod->supertypes_of.end()) {
            for (lx_symbol ss : it->second) {
              if (std::find(supers.begin(), supers.end(), ss) == supers.end() &&
                  std::find(add.begin(), add.end(), ss) == add.end()) {
                add.push_back(ss);
              }
            }
          }
        }
        if (!add.empty()) {
          supers.insert(supers.end(), add.begin(), add.end());
          changed = true;
        }
      }
    }
  }

  if (trace_) {
    trace_->log(LogLevel::Info, "module", {},
                "loaded '" + mod->id + "' lang_idx=" + std::to_string(mod->lang_idx) +
                    " epoch=" + std::to_string(mod->module_epoch));
  }
  return mod;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::load_entry_locked(
    Entry& entry, std::string* out_error) {
  if (entry.disabled || (entry.loaded && entry.loaded->disabled.load(std::memory_order_acquire))) {
    entry.disabled = true;
    if (out_error) *out_error = "language '" + entry.manifest.id + "' is disabled due to crashes";
    return nullptr;
  }
  if (entry.loaded) return entry.loaded;

  if (entry.manifest.abi_version != LX_ABI_VERSION) {
    entry.last_error = "manifest abi_version mismatch";
    if (out_error) *out_error = entry.last_error;
    return nullptr;
  }

  void* handle = nullptr;
  const lx_language_v1* vtable = nullptr;

#if defined(_WIN32)
  std::filesystem::path abs_path = std::filesystem::absolute(entry.manifest.library_path);
  std::wstring wpath = abs_path.wstring();
  HMODULE hmod = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!hmod) {
    hmod = LoadLibraryExW(wpath.c_str(), nullptr, 0);
  }
  if (!hmod) {
    DWORD err = GetLastError();
    entry.last_error = "LoadLibraryExW failed for '" + abs_path.string() +
                       "' (Win32 error " + std::to_string(err) + ")";
    if (out_error) *out_error = entry.last_error;
    return nullptr;
  }
  handle = static_cast<void*>(hmod);

  using GetLangFn = const lx_language_v1*(LX_CALL*)(uint32_t, const lx_host_api*);
  auto fn = reinterpret_cast<GetLangFn>(GetProcAddress(hmod, "lx_get_language"));
  if (!fn) {
    entry.last_error = "symbol 'lx_get_language' not found in " + abs_path.string();
    if (out_error) *out_error = entry.last_error;
    FreeLibrary(hmod);
    return nullptr;
  }

  auto g = guarded_call([&]() { vtable = fn(LX_ABI_VERSION, &host_api_); });
  if (!g.ok) {
    entry.last_error = "lx_get_language trapped for '" + entry.manifest.id + "'";
    if (out_error) *out_error = entry.last_error;
    FreeLibrary(hmod);
    return nullptr;
  }
#else
  const auto abs_path = std::filesystem::absolute(entry.manifest.library_path);
  handle = dlopen(abs_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* reason = dlerror();
    entry.last_error = "dlopen failed for '" + abs_path.string() + "': " +
                       (reason ? reason : "unknown loader error");
    if (out_error) *out_error = entry.last_error;
    return nullptr;
  }
  using GetLangFn = const lx_language_v1*(LX_CALL*)(uint32_t, const lx_host_api*);
  dlerror();
  auto fn = reinterpret_cast<GetLangFn>(dlsym(handle, "lx_get_language"));
  const char* symbol_error = dlerror();
  if (symbol_error || !fn) {
    entry.last_error = "symbol 'lx_get_language' not found in " + abs_path.string();
    if (out_error) *out_error = entry.last_error;
    dlclose(handle);
    return nullptr;
  }
  auto g = guarded_call([&]() { vtable = fn(LX_ABI_VERSION, &host_api_); });
  if (!g.ok) {
    entry.last_error = "lx_get_language failed for '" + entry.manifest.id + "'";
    if (out_error) *out_error = entry.last_error;
    dlclose(handle);
    return nullptr;
  }
#endif

  auto mod = build_module(vtable, handle, entry.lang_idx, &entry.manifest, out_error);
  if (!mod) {
    if (out_error) entry.last_error = *out_error;
    return nullptr;
  }
  entry.loaded = mod;
  return mod;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::register_static_module(
    const lx_language_v1* vtable,
    std::string* out_error,
    LanguageManifest manifest_override) {
  std::lock_guard load_lock(load_mu_);

  // Inspect info() safely to get id
  if (!vtable || !vtable->info) {
    if (out_error) *out_error = "static module missing vtable/info";
    return nullptr;
  }
  const lx_language_info* info = nullptr;
  auto g = guarded_call([&]() { info = vtable->info(); });
  if (!g.ok || !info) {
    if (out_error) *out_error = "static module info() failed";
    return nullptr;
  }
  std::string id = manifest_override.id.empty() ? std::string(to_sv(info->id)) : manifest_override.id;
  if (id.empty()) {
    if (out_error) *out_error = "static module has empty id";
    return nullptr;
  }

  uint16_t lang_idx = 0;
  {
    std::unique_lock lock(mu_);
    auto& entry = entries_[id];
    if (entry.lang_idx == 0) entry.lang_idx = alloc_lang_idx_locked();
    lang_idx = entry.lang_idx;
  }

  const LanguageManifest* mptr = manifest_override.id.empty() ? nullptr : &manifest_override;
  auto mod = build_module(vtable, nullptr, lang_idx, mptr, out_error);
  if (!mod) return nullptr;

  std::unique_lock lock(mu_);
  auto& entry = entries_[id];
  entry.manifest = mod->manifest;
  entry.loaded = mod;
  entry.disabled = false;
  for (const auto& ext : mod->manifest.extensions) {
    ext_to_id_[normalize_ext(ext)] = id;
  }
  by_lang_idx_[lang_idx] = mod;
  return mod;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::get_or_load(
    std::string_view lang_id, std::string* out_error) {
  {
    std::shared_lock lock(mu_);
    auto it = entries_.find(std::string(lang_id));
    if (it != entries_.end()) {
      if (it->second.disabled ||
          (it->second.loaded && it->second.loaded->disabled.load(std::memory_order_acquire))) {
        if (out_error) *out_error = "language '" + std::string(lang_id) + "' is disabled";
        return nullptr;
      }
      if (it->second.loaded) return it->second.loaded;
    }
  }

  std::lock_guard load_lock(load_mu_);
  std::unique_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it == entries_.end()) {
    if (out_error) *out_error = "unknown language: " + std::string(lang_id);
    return nullptr;
  }
  auto mod = load_entry_locked(it->second, out_error);
  if (mod) {
    by_lang_idx_[mod->lang_idx] = mod;
  }
  return mod;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::get_or_load_by_extension(
    std::string_view ext, std::string* out_error) {
  auto id = resolve_language_id_by_extension(ext);
  if (!id) {
    if (out_error) *out_error = "no language registered for extension: " + std::string(ext);
    return nullptr;
  }
  return get_or_load(*id, out_error);
}

std::shared_ptr<const LoadedModule> LanguageRegistry::get_or_load_for_uri(
    std::string_view uri_or_path, std::string* out_error) {
  std::string ext = extract_extension(uri_or_path);
  if (ext.empty()) {
    if (out_error) *out_error = "cannot determine file extension for: " + std::string(uri_or_path);
    return nullptr;
  }
  return get_or_load_by_extension(ext, out_error);
}

std::shared_ptr<const LoadedModule> LanguageRegistry::find_loaded(std::string_view lang_id) const {
  std::shared_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it == entries_.end()) return nullptr;
  if (it->second.disabled) return nullptr;
  return it->second.loaded;
}

std::shared_ptr<const LoadedModule> LanguageRegistry::find_loaded_by_lang_idx(uint16_t lang_idx) const {
  std::shared_lock lock(mu_);
  auto it = by_lang_idx_.find(lang_idx);
  if (it == by_lang_idx_.end()) return nullptr;
  return it->second.lock();
}

std::optional<std::string> LanguageRegistry::resolve_language_id_by_extension(
    std::string_view ext) const {
  std::string norm = normalize_ext(ext);
  std::shared_lock lock(mu_);
  auto it = ext_to_id_.find(norm);
  if (it == ext_to_id_.end()) return std::nullopt;
  return it->second;
}

bool LanguageRegistry::is_registered(std::string_view lang_id) const {
  std::shared_lock lock(mu_);
  return entries_.find(std::string(lang_id)) != entries_.end();
}

bool LanguageRegistry::is_disabled(std::string_view lang_id) const {
  std::shared_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it == entries_.end()) return false;
  if (it->second.disabled) return true;
  if (it->second.loaded && it->second.loaded->disabled.load(std::memory_order_acquire)) return true;
  return false;
}

void LanguageRegistry::reset_disabled(std::string_view lang_id) {
  std::unique_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it == entries_.end()) return;
  it->second.disabled = false;
  if (it->second.loaded) {
    it->second.loaded->crash_count.store(0, std::memory_order_relaxed);
    it->second.loaded->disabled.store(false, std::memory_order_release);
  }
}

std::shared_ptr<const LoadedModule> LanguageRegistry::reload_language(
    std::string_view lang_id,
    const std::filesystem::path& new_library_or_manifest,
    std::string* out_error) {
  std::lock_guard load_lock(load_mu_);
  if (!new_library_or_manifest.empty()) {
    std::string fname = new_library_or_manifest.filename().string();
    if (ends_with_ci(fname, ".lxmanifest.json")) {
      if (!register_manifest(new_library_or_manifest, out_error)) {
        return nullptr;
      }
    } else {
      std::unique_lock lock(mu_);
      auto it = entries_.find(std::string(lang_id));
      if (it == entries_.end()) {
        if (out_error) *out_error = "unknown language: " + std::string(lang_id);
        return nullptr;
      }
      it->second.manifest.library_path = new_library_or_manifest;
      // Clear manifest hash/version constraints when reloading direct DLL path
      it->second.manifest.grammar_hash = 0;
      it->second.manifest.lang_version = 0;
    }
  }

  std::unique_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it == entries_.end()) {
    if (out_error) *out_error = "unknown language: " + std::string(lang_id);
    return nullptr;
  }
  it->second.loaded.reset();
  it->second.disabled = false;
  auto mod = load_entry_locked(it->second, out_error);
  if (mod) {
    by_lang_idx_[mod->lang_idx] = mod;
  }
  return mod;
}

void LanguageRegistry::unload_language(std::string_view lang_id) {
  std::lock_guard load_lock(load_mu_);
  std::unique_lock lock(mu_);
  auto it = entries_.find(std::string(lang_id));
  if (it != entries_.end()) {
    it->second.loaded.reset();
  }
}

std::vector<std::string> LanguageRegistry::registered_languages() const {
  std::shared_lock lock(mu_);
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& [k, _] : entries_) {
    out.push_back(k);
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace lx::host
