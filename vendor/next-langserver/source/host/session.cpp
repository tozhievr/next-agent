#include "host/session.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace lx::host {

Session::Session(std::string uri,
                 std::shared_ptr<const LoadedModule> module,
                 std::string_view initial_text,
                 LanguageRegistry& registry,
                 ParseBackendFn parse_fn,
                 HostConfig config,
                 HostMetrics* metrics,
                 TraceLog* trace)
    : uri_(std::move(uri)),
      registry_(registry),
      parse_fn_(std::move(parse_fn)),
      config_(config),
      metrics_(metrics),
      trace_(trace),
      module_(std::move(module)),
      rope_(lx::Rope::from_utf8(initial_text, registry.host_api())) {}

void Session::set_module(std::shared_ptr<const LoadedModule> module) {
  module_ = std::move(module);
}

uint32_t Session::utf16_pos_to_byte(const lx::Rope& rope, Utf16Position pos) const noexcept {
  uint32_t line_count = rope.line_count();
  if (pos.line >= line_count) {
    return rope.byte_length();
  }
  uint32_t line_start_b = rope.line_start_byte(pos.line);
  uint32_t line_end_b = rope.line_end_byte(pos.line);
  uint32_t line_start_u16 = rope.byte_to_utf16(line_start_b);
  uint32_t line_end_u16 = rope.byte_to_utf16(line_end_b);
  uint32_t target_u16 = line_start_u16 + pos.character;
  if (target_u16 > line_end_u16) target_u16 = line_end_u16;
  return rope.utf16_to_byte(target_u16);
}

void Session::apply_byte_edit(uint32_t start, uint32_t end, std::string_view text) {
  uint32_t len = rope_.byte_length();
  if (start > len) start = len;
  if (end > len) end = len;
  if (end < start) end = start;

  rope_ = rope_.splice(start, end, text);
  lx::ChangeSet cs = lx::ChangeSet::single(start, end, static_cast<uint32_t>(text.size()));
  pending_ = pending_.compose(cs);
  doc_version_.fetch_add(1, std::memory_order_release);
}

void Session::apply_byte_edits(std::span<const ByteEdit> edits) {
  for (const auto& e : edits) {
    apply_byte_edit(e.start, e.end, e.text);
  }
}

void Session::apply_lsp_edits(std::span<const TextEdit> edits, std::optional<int64_t> lsp_ver) {
  for (const auto& e : edits) {
    if (e.is_full_replace) {
      apply_byte_edit(0, rope_.byte_length(), e.text);
    } else {
      uint32_t s = utf16_pos_to_byte(rope_, e.range.start);
      uint32_t end_b = utf16_pos_to_byte(rope_, e.range.end);
      if (end_b < s) end_b = s;
      apply_byte_edit(s, end_b, e.text);
    }
  }
  if (lsp_ver.has_value()) {
    lsp_version_.store(*lsp_ver, std::memory_order_release);
  }
}

bool Session::has_pending_edits() const noexcept {
  return !pending_.empty();
}

lx_status Session::reparse(const std::shared_ptr<CancelToken>& cancel) {
  using Clock = std::chrono::steady_clock;
  auto t_start = Clock::now();

  if (cancel && cancel->is_cancelled()) {
    if (metrics_) metrics_->reparses_cancelled.fetch_add(1, std::memory_order_relaxed);
    return LX_CANCELLED;
  }

  auto cur_mod = module_;
  if (!cur_mod) {
    return LX_BAD_STATE;
  }
  if (cur_mod->disabled.load(std::memory_order_acquire)) {
    return LX_BAD_STATE;
  }

  // Refresh module if registry reloaded a newer epoch for this language
  if (auto latest = registry_.find_loaded(cur_mod->id)) {
    if (latest->module_epoch != cur_mod->module_epoch) {
      cur_mod = latest;
      module_ = latest;
    }
  }

  if (rope_.byte_length() > config_.max_file_bytes) {
    if (trace_) {
      trace_->log(LogLevel::Warn, "reparse", uri_, "file exceeds max_file_bytes");
    }
    return LX_BAD_ARG;
  }

  auto old_snap = published_.load(std::memory_order_acquire);
  bool need_full = false;
  if (!old_snap || !old_snap->root) {
    need_full = true;
  } else if (old_snap->grammar_hash != cur_mod->info->grammar_hash ||
             old_snap->lang_version != cur_mod->info->lang_version ||
             old_snap->module_epoch != cur_mod->module_epoch) {
    need_full = true;
  } else if (pending_.change_ratio(old_snap->rope.byte_length()) > config_.full_reparse_ratio) {
    need_full = true;
  }

  // If no old snapshot and no edits yet, still parse initial document.
  // If we already have a current snapshot at the same version & module & no pending edits, skip.
  uint64_t ver = doc_version_.load(std::memory_order_acquire);
  if (!need_full && old_snap && old_snap->version == ver && pending_.empty()) {
    return LX_OK;
  }

  ParseInput pin;
  pin.module = cur_mod.get();
  pin.registry = &registry_;
  pin.rope = &rope_;
  pin.old_tree = need_full ? nullptr : old_snap->root.get();
  pin.changes = need_full ? nullptr : &pending_;
  pin.env_version = env_version_;
  pin.cancel = cancel.get();
  pin.config = config_;
  pin.force_full = need_full;

  ParseOutput pout = parse_fn_ ? parse_fn_(pin) : lex_only_parse_fallback(pin);
  if (pout.status == LX_CANCELLED || (cancel && cancel->is_cancelled())) {
    if (metrics_) metrics_->reparses_cancelled.fetch_add(1, std::memory_order_relaxed);
    if (trace_) {
      trace_->log(LogLevel::Debug, "reparse", uri_, "reparse cancelled");
    }
    return LX_CANCELLED;
  }

  auto t_diff_start = Clock::now();
  std::vector<lx::uast::Range> diff_ranges;
  if (old_snap && old_snap->root && pout.root) {
    diff_ranges = lx::uast::tree_diff(old_snap->root.get(), pout.root.get());
  } else if (pout.root && pout.root->width > 0) {
    diff_ranges.push_back(lx::uast::Range{0, pout.root->width});
  }
  auto t_diff_end = Clock::now();

  auto diags = extract_diagnostics(pout.root.get(), cur_mod.get(), &rope_);
  if (pout.truncated && diags.empty()) {
    diags.push_back(Diagnostic{
        "LX_E_TRUNCATED",
        "parse truncated by resource limits",
        lx_range{0, rope_.byte_length()},
        DiagnosticSeverity::Warning,
    });
  }

  // Update embedded language layers (§9)
  auto new_layers = LayerManager::update_layers(
      pout.root.get(),
      rope_,
      nullptr,
      cur_mod.get(),
      need_full ? std::vector<std::shared_ptr<const Layer>>{} : layers_,
      pending_,
      diff_ranges,
      registry_,
      parse_fn_,
      config_);

  auto snap = std::make_shared<Snapshot>();
  snap->version = ver;
  snap->module_epoch = cur_mod->module_epoch;
  snap->grammar_hash = cur_mod->info->grammar_hash;
  snap->lang_version = cur_mod->info->lang_version;
  snap->rope = rope_.snapshot();
  snap->root = std::move(pout.root);
  snap->changed_ranges = std::move(diff_ranges);
  snap->diagnostics = std::move(diags);
  snap->module = cur_mod;
  snap->truncated = pout.truncated;

  // Publish atomically (§6.6)
  published_.store(snap, std::memory_order_release);
  layers_ = new_layers;
  pending_ = lx::ChangeSet{};

  {
    std::lock_guard hlock(history_mu_);
    published_layers_ = std::move(new_layers);
    history_.push_back(snap);
    size_t max_keep = 1u + static_cast<size_t>(config_.history_snapshots_k);
    while (history_.size() > max_keep) {
      history_.pop_front();
    }
  }

  auto t_end = Clock::now();
  if (metrics_) {
    double total_us =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count());
    double diff_us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(t_diff_end - t_diff_start).count());
    metrics_->reparses_total.fetch_add(1, std::memory_order_relaxed);
    if (need_full) {
      metrics_->reparses_full.fetch_add(1, std::memory_order_relaxed);
    } else {
      metrics_->reparses_incremental.fetch_add(1, std::memory_order_relaxed);
    }
    metrics_->tokens_relexed.fetch_add(pout.tokens_relexed, std::memory_order_relaxed);
    metrics_->nodes_reused.fetch_add(pout.nodes_reused, std::memory_order_relaxed);
    metrics_->nodes_created.fetch_add(pout.nodes_created, std::memory_order_relaxed);
    metrics_->changed_ranges_count.fetch_add(snap->changed_ranges.size(), std::memory_order_relaxed);
    uint64_t cr_bytes = 0;
    for (const auto& r : snap->changed_ranges) cr_bytes += r.length();
    metrics_->changed_ranges_bytes.fetch_add(cr_bytes, std::memory_order_relaxed);
    metrics_->reparse_us.record(total_us);
    metrics_->tree_diff_us.record(diff_us);
    uint64_t tot_nodes = pout.nodes_reused + pout.nodes_created;
    if (tot_nodes > 0) {
      metrics_->reuse_ratio.record(static_cast<double>(pout.nodes_reused) /
                                   static_cast<double>(tot_nodes));
    }
  }

  if (trace_) {
    std::ostringstream msg;
    msg << (need_full ? "full" : "incremental") << " ver=" << snap->version
        << " tokens_relexed=" << pout.tokens_relexed
        << " reused=" << pout.nodes_reused
        << " created=" << pout.nodes_created
        << " diff_ranges=" << snap->changed_ranges.size()
        << " diags=" << snap->diagnostics.size()
        << " layers=" << layers_.size();
    trace_->log(LogLevel::Debug, "reparse", uri_, msg.str());
  }

  return pout.status;
}

std::shared_ptr<const Snapshot> Session::snapshot() const noexcept {
  return published_.load(std::memory_order_acquire);
}

std::shared_ptr<const Snapshot> Session::snapshot_at(uint64_t version) const {
  std::lock_guard lock(history_mu_);
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if ((*it)->version == version) return *it;
  }
  return nullptr;
}

std::vector<std::shared_ptr<const Snapshot>> Session::snapshot_history() const {
  std::lock_guard lock(history_mu_);
  return {history_.begin(), history_.end()};
}

std::vector<std::shared_ptr<const Layer>> Session::layers() const {
  std::lock_guard lock(history_mu_);
  return published_layers_;
}

LayeredCursor Session::layered_cursor() const {
  std::lock_guard lock(history_mu_);
  auto snap = published_.load(std::memory_order_acquire);
  return LayeredCursor(std::move(snap), published_layers_);
}

}  // namespace lx::host
