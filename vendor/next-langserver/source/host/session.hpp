#pragma once

#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "host/layer_manager.hpp"
#include "host/limits.hpp"
#include "host/metrics.hpp"
#include "host/module_loader.hpp"
#include "host/parser_adapter.hpp"
#include "host/scheduler.hpp"
#include "host/snapshot.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lx::host {

struct Utf16Position {
  uint32_t line{0};
  uint32_t character{0};
};

struct Utf16Range {
  Utf16Position start{};
  Utf16Position end{};
};

struct TextEdit {
  bool is_full_replace{false};
  Utf16Range range{};
  std::string text;
};

struct ByteEdit {
  uint32_t start{0};
  uint32_t end{0};
  std::string text;
};

/**
 * Single-writer mutable document session (§3.1, §10.1, §10.3).
 * Accumulates pending ChangeSet edits, performs incremental/full reparse,
 * updates embedded layers, keeps latest + K historical snapshots, and publishes
 * immutable Snapshots atomically.
 */
class Session {
 public:
  Session(std::string uri,
          std::shared_ptr<const LoadedModule> module,
          std::string_view initial_text,
          LanguageRegistry& registry,
          ParseBackendFn parse_fn = nullptr,
          HostConfig config = {},
          HostMetrics* metrics = nullptr,
          TraceLog* trace = nullptr);

  const std::string& uri() const noexcept { return uri_; }

  // Writer-side operations (must be serialized per session §10.1)
  void set_module(std::shared_ptr<const LoadedModule> module);
  const std::shared_ptr<const LoadedModule>& module() const noexcept { return module_; }
  void apply_byte_edit(uint32_t start, uint32_t end, std::string_view text);
  void apply_byte_edits(std::span<const ByteEdit> edits);
  void apply_lsp_edits(std::span<const TextEdit> edits, std::optional<int64_t> lsp_version = std::nullopt);
  void set_env_version(uint64_t env_version) noexcept { env_version_ = env_version; }

  bool has_pending_edits() const noexcept;
  uint64_t doc_version() const noexcept { return doc_version_.load(std::memory_order_acquire); }
  int64_t lsp_version() const noexcept { return lsp_version_.load(std::memory_order_acquire); }

  /**
   * Executes incremental or full reparse, updates layers, computes TreeDiff,
   * and atomically publishes a new Snapshot.
   * Returns LX_CANCELLED if cancelled before publishing (pending ChangeSet is preserved).
   */
  lx_status reparse(const std::shared_ptr<CancelToken>& cancel = nullptr);

  // Reader-side lock-free/O(1) snapshot & history access (§1.3, §10.1, §10.3)
  std::shared_ptr<const Snapshot> snapshot() const noexcept;
  std::shared_ptr<const Snapshot> snapshot_at(uint64_t version) const;
  std::vector<std::shared_ptr<const Snapshot>> snapshot_history() const;
  std::vector<std::shared_ptr<const Layer>> layers() const;
  LayeredCursor layered_cursor() const;

  std::mutex& writer_mutex() noexcept { return writer_mu_; }

 private:
  uint32_t utf16_pos_to_byte(const lx::Rope& rope, Utf16Position pos) const noexcept;

  std::string uri_;
  LanguageRegistry& registry_;
  ParseBackendFn parse_fn_;
  HostConfig config_;
  HostMetrics* metrics_{nullptr};
  TraceLog* trace_{nullptr};

  mutable std::mutex writer_mu_;
  std::shared_ptr<const LoadedModule> module_;
  lx::Rope rope_;
  lx::ChangeSet pending_;
  uint64_t env_version_{0};
  std::atomic<uint64_t> doc_version_{1};
  std::atomic<int64_t> lsp_version_{0};
  std::vector<std::shared_ptr<const Layer>> layers_;

  std::atomic<std::shared_ptr<const Snapshot>> published_;
  mutable std::mutex history_mu_;
  std::deque<std::shared_ptr<const Snapshot>> history_;
  std::vector<std::shared_ptr<const Layer>> published_layers_;
};

}  // namespace lx::host
