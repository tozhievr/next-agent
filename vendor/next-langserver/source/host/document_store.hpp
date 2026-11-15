#pragma once

#include "host/limits.hpp"
#include "host/metrics.hpp"
#include "host/module_loader.hpp"
#include "host/parser_adapter.hpp"
#include "host/scheduler.hpp"
#include "host/session.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lx::host {

/**
 * Manages open document Sessions, coordinates debounced/prioritized reparses via
 * Scheduler, and notifies subscribers (e.g. LSP publishDiagnostics) on new Snapshots.
 */
class DocumentStore {
 public:
  using PublishCallback = std::function<void(const std::string& uri,
                                             const std::shared_ptr<const Snapshot>& snap,
                                             const std::vector<Diagnostic>& all_diags,
                                             int64_t lsp_version)>;

  DocumentStore(LanguageRegistry& registry,
                Scheduler& scheduler,
                ParseBackendFn parse_fn = nullptr,
                HostConfig config = {},
                HostMetrics* metrics = nullptr,
                TraceLog* trace = nullptr);
  ~DocumentStore();

  void set_publish_callback(PublishCallback cb);

  /**
   * Opens a document. Resolves language via `language_id` (if non-empty) or URI extension.
   * Performs immediate initial parse (or schedules immediate task) and returns the Session.
   */
  std::shared_ptr<Session> open_document(
      std::string uri,
      std::string_view text,
      std::string_view language_id = {},
      int64_t lsp_version = 0,
      bool sync_parse = true,
      std::string* out_error = nullptr);

  /**
   * Applies LSP incremental or full edits to an open document and schedules debounced reparse.
   */
  bool apply_edits(const std::string& uri,
                   std::span<const TextEdit> edits,
                   std::optional<int64_t> lsp_version = std::nullopt,
                   TaskPriority priority = TaskPriority::Interactive);

  /**
   * Applies byte-offset edits to an open document and schedules debounced reparse.
   */
  bool apply_byte_edits(const std::string& uri,
                        std::span<const ByteEdit> edits,
                        TaskPriority priority = TaskPriority::Interactive);

  /**
   * Closes a document and removes its Session (releasing its Snapshot & LoadedModule refs).
   */
  void close_document(const std::string& uri);

  std::shared_ptr<Session> get_session(const std::string& uri) const;

  /**
   * Ensures any pending edits for `uri` are reparsed before returning the latest Snapshot.
   */
  std::shared_ptr<const Snapshot> ensure_parsed(const std::string& uri);

  /**
   * Triggers a full reparse for all open documents of `lang_id` (e.g. after hot reload §10.3).
   */
  void notify_language_reloaded(std::string_view lang_id);

  std::vector<std::string> open_uris() const;

 private:
  void schedule_reparse(const std::shared_ptr<Session>& session,
                        TaskPriority priority,
                        std::chrono::milliseconds debounce);

  LanguageRegistry& registry_;
  Scheduler& scheduler_;
  ParseBackendFn parse_fn_;
  HostConfig config_;
  HostMetrics* metrics_{nullptr};
  TraceLog* trace_{nullptr};

  mutable std::mutex cb_mu_;
  PublishCallback publish_cb_;

  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
};

}  // namespace lx::host
