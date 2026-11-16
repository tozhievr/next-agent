#pragma once

#include "host/document_store.hpp"
#include "host/limits.hpp"
#include "host/lsp_transport.hpp"
#include "host/metrics.hpp"
#include "host/module_loader.hpp"
#include "host/scheduler.hpp"

#include <atomic>
#include <istream>
#include <ostream>
#include <string>

namespace lx::host {

/**
 * Minimal LSP stdio JSON-RPC 2.0 server (§3, §11).
 * Supports initialize, didOpen, didChange (incremental UTF-16), didClose,
 * semanticTokens/full & range, foldingRange, documentSymbol, selectionRange,
 * publishDiagnostics, shutdown, exit.
 */
class LspServer {
 public:
  explicit LspServer(HostConfig config = {}, ParseBackendFn parse_fn = nullptr);
  ~LspServer();

  LanguageRegistry& registry() noexcept { return registry_; }
  DocumentStore& store() noexcept { return store_; }
  HostMetrics& metrics() noexcept { return metrics_; }
  TraceLog& trace() noexcept { return trace_; }
  Scheduler& scheduler() noexcept { return scheduler_; }

  /**
   * Runs the JSON-RPC message loop over `in` / `out` until EOF or `exit`.
   * Returns exit code (0 on clean shutdown+exit, 1 otherwise).
   */
  int run(std::istream& in, std::ostream& out);

  /**
   * Handles a single parsed JSON-RPC message (exposed for unit testing).
   */
  void handle_message(const JsonValue& msg, LspTransport& transport);

  bool exit_requested() const noexcept { return exit_requested_.load(std::memory_order_acquire); }

 private:
  JsonValue make_lsp_range(const lx::Rope& rope, lx_range r) const;
  JsonValue make_lsp_position(const lx::Rope& rope, uint32_t byte_off) const;
  uint32_t lsp_pos_to_byte(const lx::Rope& rope, const JsonValue& pos_obj) const;

  void handle_initialize(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void handle_did_open(const JsonValue& params);
  void handle_did_change(const JsonValue& params);
  void handle_did_close(const JsonValue& params, LspTransport& tr);
  void handle_semantic_tokens_full(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void handle_semantic_tokens_range(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void handle_folding_range(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void handle_document_symbol(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void handle_selection_range(const JsonValue& id, const JsonValue& params, LspTransport& tr);
  void publish_diagnostics(const std::string& uri,
                           const std::shared_ptr<const Snapshot>& snap,
                           const std::vector<Diagnostic>& diags,
                           int64_t lsp_version,
                           LspTransport& tr);

  HostConfig config_;
  HostMetrics metrics_;
  TraceLog trace_;
  LanguageRegistry registry_;
  Scheduler scheduler_;
  DocumentStore store_;

  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> exit_requested_{false};
  LspTransport* active_transport_{nullptr};
};

}  // namespace lx::host
