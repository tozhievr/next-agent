#include "engine/parse/parse.h"
#include "engine/parse/parse_internal.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

#include "engine/common/hash.h"

namespace lx::parse {

namespace {

struct OwnedGreenRef {
  uast::GreenPtr node;
  uast::FieldId field = uast::kNoField;
};

struct LoopFrame {
  uint32_t entry_state = atn::kIndexNoneU32;       // STAR_LOOP_ENTRY or PLUS_LOOP_BACK
  uint32_t block_start_state = atn::kIndexNoneU32; // STAR_BLOCK_START or PLUS_BLOCK_START
  uint32_t loop_end_state = atn::kIndexNoneU32;
  bool spliceable = false;
  bool is_plus = false;

  uast::GreenPtr seq_acc;
  std::vector<OwnedGreenRef> items;
  uint64_t max_la_end = 0;
  uint32_t ctx_depth = 0;
  uint32_t pred_deps_hash = 0;

  const uast::GreenNode* old_seq = nullptr;
  uint32_t old_seq_start = 0;

  uint32_t iter_start_byte = 0;
  uint32_t iter_start_item_count = 0;
  lx_symbol body_entry_symbol = LX_SYMBOL_INVALID;
  bool body_single_item = false;
};

struct RuleFrame {
  uint16_t rule_id = 0;
  uint32_t current_state = 0;
  uint32_t return_state = atn::kIndexNoneU32;
  uint16_t prec = 0;
  uint16_t caller_prec = 0;
  lx_field target_field = LX_FIELD_NONE;

  uast::Kind node_kind = 0;
  uint32_t node_flags = uast::FLAG_RULE;
  uint64_t max_la_end = 0;
  uint32_t ctx_depth = 0;
  uint32_t pred_deps_hash = 0;

  bool is_left_recursive = false;
  bool is_flattened = false;
  bool inside_prec_loop = false;
  uast::GreenPtr lr_left;
  lx_field lr_lhs_field = LX_FIELD_NONE;
  uast::Kind lr_alt_kind = 0;
  uint32_t lr_alt_flags = uast::FLAG_RULE;
  uint32_t lr_iter_start_byte = 0;

  std::vector<OwnedGreenRef> children;
  std::vector<LoopFrame> loops;
};

uint32_t combine_pred_hash(uint32_t h, uint32_t v) noexcept {
  if (v == 0) return h;
  uint32_t out = lx::hash_combine_32(h == 0 ? lx::kFnv1a32OffsetBasis : h, v);
  return out == 0 ? 1u : out;
}

uint32_t combine_dep_hash(uint32_t h, uint32_t dep_id, uint64_t val) noexcept {
  uint32_t base = (h == 0) ? lx::kFnv1a32OffsetBasis : h;
  base = lx::hash_combine_32(base, dep_id);
  base = lx::hash_combine_32(base, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
  base = lx::hash_combine_32(base, static_cast<uint32_t>(val >> 32));
  return base == 0 ? 1u : base;
}

std::vector<uast::GreenRef> to_raw_refs(const std::vector<OwnedGreenRef>& owned) {
  std::vector<uast::GreenRef> raw;
  raw.reserve(owned.size());
  for (const auto& o : owned) {
    raw.push_back(uast::GreenRef{o.node.get(), o.field});
  }
  return raw;
}

uast::GreenPtr make_token_or_intern(uast::TokenInterner* interner,
                                    uast::Kind kind,
                                    uint32_t width,
                                    uint32_t reach_ahead,
                                    uint32_t flags,
                                    uint32_t text_hash,
                                    uint32_t state_in,
                                    uint32_t state_out) {
  if (interner) {
    return interner->intern_or_create(kind, width, reach_ahead, flags,
                                      text_hash, state_in, state_out);
  }
  return uast::make_token(kind, width, reach_ahead, flags,
                          text_hash, state_in, state_out);
}

uast::GreenPtr ensure_token_green(const LexToken& tok,
                                  uint16_t lang_idx,
                                  uast::TokenInterner* interner) {
  if (tok.green) {
    return tok.green;
  }
  uint32_t flags = uast::FLAG_LEAF;
  if (tok.is_hidden()) flags |= uast::FLAG_HIDDEN;
  if (tok.is_error) flags |= uast::FLAG_ERROR;
  return make_token_or_intern(
      interner,
      uast::make_kind(lang_idx, tok.symbol),
      tok.width,
      tok.reach_ahead,
      flags,
      tok.text_hash,
      tok.lex_state_in,
      tok.lex_state_out);
}

class ParserSession final : public internal::LookaheadAdapter {
 public:
  ParserSession(const ParseOptions& opts,
                const atn::Atn& atn,
                DfaCache* dfa_cache,
                TokenStream& stream,
                const ReuseOracle* oracle)
      : opts_(opts),
        atn_(atn),
        dfa_cache_(dfa_cache),
        stream_(stream),
        oracle_(oracle),
        start_time_(std::chrono::steady_clock::now()) {
    build_atn_helpers();
  }

  // LookaheadAdapter
  const LexToken& peek(uint32_t k) override {
    const LexToken& tok = stream_.peek(k);
    record_la_token(tok);
    return tok;
  }

  int eval_predicate(uint32_t pred_id, uint32_t pred_flags, uint32_t k_offset) override {
    record_pred_deps(pred_id);
    if (!opts_.lang || !opts_.lang->eval_pred) {
      return 1;
    }

    PredViewCtx pv{this, k_offset, {}};
    lx_pred_view view{};
    view.ctx = &pv;
    view.la_symbol = &pred_la_symbol;
    view.la_text = &pred_la_text;
    view.la_line_start = &pred_la_line_start;
    view.env_get = &pred_env_get;

    int res = opts_.lang->eval_pred(pred_id, &view);
    if (pred_flags & atn::kTransPredNegated) {
      res = (res == 0) ? 1 : 0;
    }
    return res;
  }

  bool is_cancelled() const noexcept override {
    if (opts_.cancel_flag && opts_.cancel_flag->load(std::memory_order_relaxed)) {
      return true;
    }
    return false;
  }

  ParseResult execute(uint16_t start_rule_id) {
    ParseResult result;
    if (start_rule_id >= atn_.rules().size()) {
      result.status = LX_BAD_ARG;
      return result;
    }

    push_rule_frame(start_rule_id, 0u, LX_FIELD_NONE, atn::kIndexNoneU32);

    uint64_t step_count = 0;
    while (!frames_.empty()) {
      if ((++step_count & 0xFFu) == 0u) {
        if (is_cancelled()) {
          result.status = LX_CANCELLED;
          return result;
        }
        if (check_time_limit()) {
          trigger_truncation(DiagnosticCode::MaxParseMs, "parse time limit exceeded");
          break;
        }
      }

      if (truncated_) {
        break;
      }

      RuleFrame& rf = frames_.back();
      const auto& st = atn_.state(rf.current_state);

      if (st.kind == atn::StateKind::RuleStop) {
        handle_rule_stop();
        continue;
      }

      if (st.kind == atn::StateKind::StarLoopEntry) {
        handle_star_loop_entry(rf, st);
        continue;
      }

      if (st.kind == atn::StateKind::StarLoopBack) {
        handle_star_loop_back(rf, st);
        continue;
      }

      if (st.kind == atn::StateKind::PlusBlockStart) {
        if (handle_plus_block_start(rf, st)) {
          continue;
        }
        if (truncated_) break;
        // Fall through to decision / transition at PLUS_BLOCK_START.
      }

      if (st.kind == atn::StateKind::PlusLoopBack) {
        handle_plus_loop_back(rf, st);
        continue;
      }

      if (st.kind == atn::StateKind::LoopEnd) {
        handle_loop_end(rf, st);
        continue;
      }

      const auto trans = atn_.state_transitions(rf.current_state);
      if (trans.empty()) {
        // Dead end (no outgoing transitions from non-RuleStop state).
        rf.current_state = atn_.rule(rf.rule_id).stop_state;
        continue;
      }

      if (st.decision_id != atn::kIndexNoneU16 || trans.size() > 1) {
        handle_decision(rf, st, trans);
        continue;
      }

      // Single outgoing transition.
      execute_transition(rf, trans[0]);
    }

    if (truncated_) {
      unwind_truncated();
    }

    result.status = LX_OK;
    result.root = std::move(root_);
    result.truncated = truncated_;
    result.stats = stats_;
    const auto& ts_stats = stream_.stats();
    result.stats.tokens_lexed = ts_stats.lex_calls;
    result.stats.tokens_reidentified = ts_stats.reidentified_tokens;
    result.stats.tokens_synced = ts_stats.synced_tokens;
    result.env_deps_snapshot = std::move(env_deps_snapshot_);
    return result;
  }

  std::vector<Diagnostic> extra_limit_diagnostics;

 private:
  struct PredViewCtx {
    ParserSession* session = nullptr;
    uint32_t k_offset = 0;
    std::deque<std::string> temp_strings;
  };

  const LexToken* resolve_pred_token(uint32_t k_offset, int32_t i) {
    if (i == 0) {
      return nullptr;
    }
    if (i > 0) {
      const uint32_t idx = k_offset + static_cast<uint32_t>(i - 1);
      return &peek(idx);
    }
    // i < 0
    const int64_t shifted = static_cast<int64_t>(k_offset) + static_cast<int64_t>(i);
    if (shifted >= 0) {
      return &peek(static_cast<uint32_t>(shifted));
    }
    return stream_.lookback(static_cast<int32_t>(shifted));
  }

  static lx_symbol LX_CALL pred_la_symbol(void* ctx, int32_t i) {
    auto* pv = static_cast<PredViewCtx*>(ctx);
    const LexToken* tok = pv->session->resolve_pred_token(pv->k_offset, i);
    return tok ? tok->symbol : LX_SYMBOL_INVALID;
  }

  static lx_str LX_CALL pred_la_text(void* ctx, int32_t i) {
    auto* pv = static_cast<PredViewCtx*>(ctx);
    const LexToken* tok = pv->session->resolve_pred_token(pv->k_offset, i);
    if (!tok) return lx_str{nullptr, 0};
    pv->temp_strings.push_back(pv->session->stream_.token_text(*tok));
    const std::string& s = pv->temp_strings.back();
    return lx_str{s.data(), static_cast<uint32_t>(s.size())};
  }

  static int LX_CALL pred_la_line_start(void* ctx, int32_t i) {
    auto* pv = static_cast<PredViewCtx*>(ctx);
    const LexToken* tok = pv->session->resolve_pred_token(pv->k_offset, i);
    return (tok && pv->session->stream_.is_line_start(*tok)) ? 1 : 0;
  }

  static uint64_t LX_CALL pred_env_get(void* ctx, uint32_t dep_id) {
    auto* pv = static_cast<PredViewCtx*>(ctx);
    const uint64_t val = pv->session->opts_.env.get(dep_id);
    pv->session->record_env_dep_value(dep_id, val);
    return val;
  }

  void record_env_dep_value(uint32_t dep_id, uint64_t val) {
    auto it = env_dep_seen_.find(dep_id);
    if (it == env_dep_seen_.end()) {
      env_dep_seen_.emplace(dep_id, val);
      env_deps_snapshot_.emplace_back(dep_id, val);
    }
    if (!frames_.empty()) {
      frames_.back().pred_deps_hash =
          combine_dep_hash(frames_.back().pred_deps_hash, dep_id, val);
      if (!frames_.back().loops.empty()) {
        frames_.back().loops.back().pred_deps_hash =
            combine_dep_hash(frames_.back().loops.back().pred_deps_hash, dep_id, val);
      }
    }
  }

  void record_pred_deps(uint32_t pred_id) {
    if (pred_id >= atn_.preds().size()) return;
    for (uint32_t dep_id : atn_.pred_deps(pred_id)) {
      const uint64_t val = opts_.env.get(dep_id);
      record_env_dep_value(dep_id, val);
    }
  }

  void record_la_token(const LexToken& tok) noexcept {
    const uint64_t extra = tok.is_eof()
                               ? std::max<uint32_t>(1u, tok.reach_ahead)
                               : static_cast<uint64_t>(tok.width) + tok.reach_ahead;
    const uint64_t end = static_cast<uint64_t>(tok.new_offset) + extra;
    if (!frames_.empty()) {
      frames_.back().max_la_end = std::max(frames_.back().max_la_end, end);
      if (!frames_.back().loops.empty()) {
        frames_.back().loops.back().max_la_end =
            std::max(frames_.back().loops.back().max_la_end, end);
      }
    }
  }

  void propagate_la_and_ctx(uint32_t used_ctx_depth) noexcept {
    if (used_ctx_depth == 0 || frames_.empty()) return;
    RuleFrame& cur = frames_.back();
    cur.ctx_depth = std::max(cur.ctx_depth, used_ctx_depth);
    if (!cur.loops.empty()) {
      cur.loops.back().ctx_depth = std::max(cur.loops.back().ctx_depth, used_ctx_depth);
    }
    const size_t n = frames_.size();
    const size_t count = std::min<size_t>(used_ctx_depth, n - 1);
    for (size_t i = 1; i <= count; ++i) {
      RuleFrame& anc = frames_[n - 1 - i];
      anc.max_la_end = std::max(anc.max_la_end, cur.max_la_end);
      if (!anc.loops.empty()) {
        anc.loops.back().max_la_end =
            std::max(anc.loops.back().max_la_end, cur.max_la_end);
      }
      const uint32_t anc_ctx =
          (used_ctx_depth > i) ? static_cast<uint32_t>(used_ctx_depth - i) : 0u;
      if (anc_ctx > 0) {
        anc.ctx_depth = std::max(anc.ctx_depth, anc_ctx);
        if (!anc.loops.empty()) {
          anc.loops.back().ctx_depth = std::max(anc.loops.back().ctx_depth, anc_ctx);
        }
      }
    }
  }

  void build_atn_helpers() {
    const size_t n_states = atn_.states().size();
    plus_start_to_loop_back_.assign(n_states, atn::kIndexNoneU32);
    for (size_t sid = 0; sid < n_states; ++sid) {
      const auto& st = atn_.state(static_cast<uint32_t>(sid));
      if (st.kind == atn::StateKind::PlusLoopBack &&
          st.pair_state < n_states) {
        plus_start_to_loop_back_[st.pair_state] = static_cast<uint32_t>(sid);
      }
    }

    const size_t n_rules = atn_.rules().size();
    rule_accepted_kinds_.resize(n_rules);
    for (size_t rid = 0; rid < n_rules; ++rid) {
      const auto& r = atn_.rule(static_cast<uint16_t>(rid));
      auto& vec = rule_accepted_kinds_[rid];
      vec.push_back(uast::make_kind(opts_.lang_idx, r.symbol_id));
      for (const auto& al : atn_.rule_alt_labels(static_cast<uint16_t>(rid))) {
        const uast::Kind k = uast::make_kind(opts_.lang_idx, al.symbol_id);
        if (std::find(vec.begin(), vec.end(), k) == vec.end()) {
          vec.push_back(k);
        }
      }
    }
  }

  void analyze_loop_body(uint32_t block_start_state,
                         lx_symbol& out_entry_sym,
                         bool& out_single_item) const {
    out_entry_sym = LX_SYMBOL_INVALID;
    out_single_item = false;
    uint32_t cur = block_start_state;
    // Walk up to 9 linear epsilon transitions to find the first real element.
    for (int hop = 0; hop < 9; ++hop) {
      const auto& st = atn_.state(cur);
      if (st.kind == atn::StateKind::BlockEnd) {
        return;
      }
      const auto ts = atn_.state_transitions(cur);
      if (ts.size() != 1) {
        return;
      }
      const auto& tr = ts[0];
      if (tr.kind == atn::TransitionKind::Epsilon ||
          tr.kind == atn::TransitionKind::Action) {
        cur = tr.to;
        continue;
      }
      if (tr.kind == atn::TransitionKind::Rule) {
        out_entry_sym = atn_.rule(static_cast<uint16_t>(tr.arg0)).symbol_id;
      } else if (tr.kind == atn::TransitionKind::Atom) {
        out_entry_sym = static_cast<lx_symbol>(tr.arg0);
      } else {
        return;
      }
      // Check if next state immediately reaches BlockEnd via epsilons.
      uint32_t after = tr.to;
      for (int hop2 = 0; hop2 < 4; ++hop2) {
        const auto& st2 = atn_.state(after);
        if (st2.kind == atn::StateKind::BlockEnd) {
          out_single_item = true;
          return;
        }
        const auto ts2 = atn_.state_transitions(after);
        if (ts2.size() == 1 &&
            (ts2[0].kind == atn::TransitionKind::Epsilon ||
             ts2[0].kind == atn::TransitionKind::Action)) {
          after = ts2[0].to;
          continue;
        }
        break;
      }
      return;
    }
  }

  bool item_matches_loop_entry(const uast::GreenNode* item,
                               uint16_t loop_entry_rule_hint,
                               lx_symbol entry_sym) const noexcept {
    if (!item || entry_sym == LX_SYMBOL_INVALID) {
      return false;
    }
    if (item->is_hidden()) {
      return false;
    }
    const lx_symbol sym = uast::kind_symbol(item->kind);
    if (sym == entry_sym) {
      return true;
    }
    if (loop_entry_rule_hint < rule_accepted_kinds_.size()) {
      for (uast::Kind k : rule_accepted_kinds_[loop_entry_rule_hint]) {
        if (item->kind == k) return true;
      }
    }
    return false;
  }

  uint16_t find_loop_entry_rule_hint(uint32_t block_start_state) const noexcept {
    uint32_t cur = block_start_state;
    for (int hop = 0; hop < 9; ++hop) {
      const auto ts = atn_.state_transitions(cur);
      if (ts.size() != 1) return atn::kIndexNoneU16;
      if (ts[0].kind == atn::TransitionKind::Epsilon ||
          ts[0].kind == atn::TransitionKind::Action) {
        cur = ts[0].to;
        continue;
      }
      if (ts[0].kind == atn::TransitionKind::Rule) {
        return static_cast<uint16_t>(ts[0].arg0);
      }
      return atn::kIndexNoneU16;
    }
    return atn::kIndexNoneU16;
  }

  bool check_time_limit() const noexcept {
    if (opts_.limits.max_parse_ms == 0) return false;
    const auto now = std::chrono::steady_clock::now();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    return ms >= static_cast<int64_t>(opts_.limits.max_parse_ms);
  }

  void trigger_truncation(DiagnosticCode code, std::string msg) {
    if (truncated_) return;
    truncated_ = true;
    Diagnostic d;
    d.code = code;
    d.severity = DiagnosticSeverity::Error;
    const uint32_t pos = stream_.new_offset();
    d.range = uast::Range{pos, pos};
    d.message = std::move(msg);
    extra_limit_diagnostics.push_back(std::move(d));
  }

  void check_node_limit() {
    ++stats_.nodes_created;
    if (opts_.limits.max_nodes > 0 &&
        stats_.nodes_created > opts_.limits.max_nodes) {
      trigger_truncation(DiagnosticCode::MaxNodes, "max node limit exceeded");
    }
  }

  void append_child_to_current(uast::GreenPtr node, lx_field field_id) {
    if (!node || frames_.empty()) return;
    check_node_limit();
    RuleFrame& rf = frames_.back();

    if (node->is_inner()) {
      const auto* in = node->as_inner();
      if (in->ctx_depth > 1) {
        const uint32_t prop = in->ctx_depth - 1;
        rf.ctx_depth = std::max(rf.ctx_depth, prop);
        if (!rf.loops.empty()) {
          rf.loops.back().ctx_depth = std::max(rf.loops.back().ctx_depth, prop);
        }
      }
      if (in->pred_deps_hash != 0) {
        rf.pred_deps_hash = combine_pred_hash(rf.pred_deps_hash, in->pred_deps_hash);
        if (!rf.loops.empty()) {
          rf.loops.back().pred_deps_hash =
              combine_pred_hash(rf.loops.back().pred_deps_hash, in->pred_deps_hash);
        }
      }
    }

    if (!rf.loops.empty()) {
      rf.loops.back().items.push_back(OwnedGreenRef{std::move(node), field_id});
    } else {
      rf.children.push_back(OwnedGreenRef{std::move(node), field_id});
    }
  }

  LexToken consume_next_token() {
    LexToken tok = stream_.next();
    consumed_end_byte_ = tok.new_offset + tok.width;
    return tok;
  }

  void jump_stream(uint32_t new_pos, uint32_t lex_state_out) {
    stream_.jump(new_pos, lex_state_out);
    consumed_end_byte_ = new_pos;
  }

  void flush_hidden_to_current() {
    auto hidden = stream_.take_hidden();
    for (const LexToken& ht : hidden) {
      consumed_end_byte_ = ht.new_offset + ht.width;
      uast::GreenPtr g = ensure_token_green(ht, opts_.lang_idx, opts_.token_interner);
      append_child_to_current(std::move(g), LX_FIELD_NONE);
      if (truncated_) break;
    }
  }

  uint32_t current_byte_pos() noexcept {
    return stream_.new_offset();
  }

  void push_rule_frame(uint16_t rule_id,
                       uint16_t prec,
                       lx_field target_field,
                       uint32_t return_state) {
    if (frames_.size() >= opts_.limits.max_tree_depth) {
      trigger_truncation(DiagnosticCode::MaxDepth, "max tree depth exceeded");
      return;
    }
    const auto& r = atn_.rule(rule_id);
    RuleFrame rf;
    rf.rule_id = rule_id;
    rf.current_state = r.start_state;
    rf.return_state = return_state;
    rf.prec = prec;
    rf.caller_prec = frames_.empty() ? 0u : frames_.back().prec;
    rf.target_field = target_field;
    rf.node_kind = uast::make_kind(opts_.lang_idx, r.symbol_id);
    rf.node_flags = uast::FLAG_RULE;
    if (r.flags & atn::kRuleSupertype) {
      rf.node_flags |= uast::FLAG_SUPERTYPE;
    }
    rf.is_left_recursive = (r.flags & atn::kRuleLeftRecursive) != 0;
    rf.is_flattened = (r.flags & atn::kRuleFlatten) != 0;
    if (return_state != atn::kIndexNoneU32 && !frames_.empty()) {
      caller_stack_.push_back(internal::ActiveCallerFrame{return_state, frames_.back().prec});
      caller_return_states_.push_back(return_state);
    }
    frames_.push_back(std::move(rf));
  }

  void pop_rule_frame() {
    const bool had_caller = frames_.back().return_state != atn::kIndexNoneU32;
    frames_.pop_back();
    if (had_caller && !caller_stack_.empty()) {
      caller_stack_.pop_back();
      caller_return_states_.pop_back();
    }
  }

  uast::GreenPtr build_rule_node(const RuleFrame& rf,
                                 uast::Kind kind,
                                 uint32_t flags,
                                 const std::vector<OwnedGreenRef>& owned,
                                 uint32_t extra_reach_override = 0) {
    check_node_limit();
    const uint32_t end_pos = consumed_end_byte_;
    uint32_t extra_reach =
        (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
    extra_reach = std::max(extra_reach, extra_reach_override);
    const uint32_t ctx_hash =
        compute_ctx_hash(caller_return_states_, rf.ctx_depth);

    if (owned.size() > uast::kSeqThreshold) {
      uast::GreenPtr seq;
      std::vector<uast::GreenRef> pending;
      pending.reserve(owned.size());
      auto flush_pending = [&]() {
        if (pending.empty()) return;
        uast::GreenPtr chunk = uast::seq_from_items(pending);
        seq = uast::seq_concat(std::move(seq), std::move(chunk));
        pending.clear();
      };
      for (const auto& item : owned) {
        if (!item.node) continue;
        if (item.node->is_seq()) {
          flush_pending();
          seq = uast::seq_concat(std::move(seq), uast::GreenPtr(item.node.get()));
        } else {
          pending.push_back(uast::GreenRef{item.node.get(), item.field});
        }
      }
      flush_pending();
      if (seq) {
        seq = uast::seq_with_context(seq.get(), rf.ctx_depth, ctx_hash,
                                     rf.pred_deps_hash, extra_reach);
        const uast::GreenRef seq_ref{seq.get(), uast::kNoField};
        return uast::make_inner(kind, flags, std::span<const uast::GreenRef>(&seq_ref, 1),
                                rf.ctx_depth, ctx_hash, rf.prec,
                                rf.pred_deps_hash, extra_reach);
      }
    }
    std::vector<uast::GreenRef> raw = to_raw_refs(owned);
    return uast::make_inner(kind, flags, raw, rf.ctx_depth, ctx_hash,
                            rf.prec, rf.pred_deps_hash, extra_reach);
  }

  void consume_trailing_to_eof(RuleFrame& rf) {
    while (!peek(0).is_eof() && !truncated_) {
      if (is_cancelled()) {
        truncated_ = true;
        return;
      }
      std::vector<OwnedGreenRef> err_tokens;
      const uint32_t max_toks = std::max<uint32_t>(1u, opts_.limits.max_error_tokens);
      while (err_tokens.size() < max_toks && !peek(0).is_eof()) {
        if (err_tokens.empty()) {
          flush_hidden_to_current();
        } else {
          for (const LexToken& ht : stream_.take_hidden()) {
            consumed_end_byte_ = ht.new_offset + ht.width;
            uast::GreenPtr hg = ensure_token_green(ht, opts_.lang_idx, opts_.token_interner);
            err_tokens.push_back(OwnedGreenRef{std::move(hg), LX_FIELD_NONE});
          }
        }
        LexToken consumed = consume_next_token();
        uast::GreenPtr g = ensure_token_green(consumed, opts_.lang_idx, opts_.token_interner);
        err_tokens.push_back(OwnedGreenRef{std::move(g), LX_FIELD_NONE});
      }
      if (!err_tokens.empty()) {
        const uint32_t end_pos = consumed_end_byte_;
        const uint32_t extra =
            (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
        std::vector<uast::GreenRef> raw = to_raw_refs(err_tokens);
        uast::GreenPtr err_node = uast::make_error(
            uast::make_kind(opts_.lang_idx, LX_SYMBOL_ERROR), raw, extra);
        append_child_to_current(std::move(err_node), LX_FIELD_NONE);
      }
    }
  }

  uast::GreenPtr finalize_rule_frame_node(RuleFrame& rf, uint32_t extra_flags = 0) {
    if (rf.is_left_recursive && !rf.is_flattened && rf.lr_left) {
      if (!rf.children.empty()) {
        std::vector<OwnedGreenRef> combined;
        combined.reserve(1 + rf.children.size());
        combined.push_back(OwnedGreenRef{std::move(rf.lr_left), rf.lr_lhs_field});
        for (auto& c : rf.children) {
          combined.push_back(std::move(c));
        }
        rf.children.clear();
        rf.lr_lhs_field = LX_FIELD_NONE;
        const uast::Kind k = rf.lr_alt_kind ? rf.lr_alt_kind : rf.node_kind;
        const uint32_t fl =
            (rf.lr_alt_kind ? rf.lr_alt_flags : rf.node_flags) | extra_flags;
        return build_rule_node(rf, k, fl, combined);
      }

      const uint32_t end_pos = consumed_end_byte_;
      const uint32_t extra_reach =
          (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
      const auto* left_in = rf.lr_left->as_inner();
      const bool need_update =
          left_in &&
          (extra_flags != 0 ||
           extra_reach > left_in->reach_ahead ||
           rf.ctx_depth > left_in->ctx_depth ||
           (rf.pred_deps_hash != 0 && rf.pred_deps_hash != left_in->pred_deps_hash));
      if (need_update) {
        const uint32_t ctx_d = std::max(left_in->ctx_depth, rf.ctx_depth);
        const uint32_t ctx_h = compute_ctx_hash(caller_return_states_, ctx_d);
        const uint32_t pred_h =
            combine_pred_hash(left_in->pred_deps_hash, rf.pred_deps_hash);
        return uast::make_inner(
            left_in->kind, left_in->flags | extra_flags, left_in->children(),
            ctx_d, ctx_h, rf.prec, pred_h,
            std::max(left_in->reach_ahead, extra_reach));
      }
      return std::move(rf.lr_left);
    }
    return build_rule_node(rf, rf.node_kind, rf.node_flags | extra_flags, rf.children);
  }

  void handle_rule_stop() {
    RuleFrame& rf = frames_.back();
    // Close any unclosed loops safely.
    while (!rf.loops.empty()) {
      finalize_and_pop_loop(rf);
    }
    if (frames_.size() == 1) {
      if (!truncated_) {
        consume_trailing_to_eof(rf);
      }
      flush_hidden_to_current();
    }

    uast::GreenPtr node = finalize_rule_frame_node(rf, 0u);

    const uint32_t ret_state = rf.return_state;
    const lx_field field = rf.target_field;
    pop_rule_frame();

    if (frames_.empty()) {
      root_ = std::move(node);
    } else {
      append_child_to_current(std::move(node), field);
      frames_.back().current_state = ret_state;
    }
  }

  void flush_loop_items_to_seq(LoopFrame& loop) {
    if (loop.items.empty()) return;
    std::vector<uast::GreenRef> raw = to_raw_refs(loop.items);
    uast::GreenPtr chunk = uast::seq_from_items(raw);
    loop.seq_acc = uast::seq_concat(std::move(loop.seq_acc), std::move(chunk));
    loop.items.clear();
  }

  void finalize_and_pop_loop(RuleFrame& rf) {
    LoopFrame loop = std::move(rf.loops.back());
    rf.loops.pop_back();

    const uint32_t end_pos = consumed_end_byte_;
    const uint32_t extra_reach =
        (loop.max_la_end > end_pos) ? static_cast<uint32_t>(loop.max_la_end - end_pos) : 0u;
    const uint32_t ctx_h = compute_ctx_hash(caller_return_states_, loop.ctx_depth);

    if (loop.seq_acc) {
      flush_loop_items_to_seq(loop);
      if (loop.seq_acc) {
        uast::GreenPtr seq = uast::seq_with_context(
            loop.seq_acc.get(), loop.ctx_depth, ctx_h,
            loop.pred_deps_hash, extra_reach);
        const uint32_t cnt = uast::seq_count(seq.get());
        if (cnt <= uast::kSeqThreshold) {
          // Unwrap into parent RuleFrame / enclosing LoopFrame cleanly.
          auto append_unwrapped = [&](const uast::GreenNode* s, auto& self) -> void {
            if (!s) return;
            if (!s->is_seq()) {
              append_child_to_current(uast::GreenPtr(s), LX_FIELD_NONE);
              return;
            }
            for (const auto& r : s->as_inner()->children()) {
              if (r.node && r.node->is_seq()) {
                self(r.node, self);
              } else if (r.node) {
                append_child_to_current(uast::GreenPtr(r.node), r.field_id);
              }
            }
          };
          append_unwrapped(seq.get(), append_unwrapped);
        } else {
          check_node_limit();
          if (!rf.loops.empty()) {
            // Nested loop: flatten into enclosing loop's seq_acc.
            flush_loop_items_to_seq(rf.loops.back());
            rf.loops.back().seq_acc =
                uast::seq_concat(std::move(rf.loops.back().seq_acc), std::move(seq));
          } else {
            rf.children.push_back(OwnedGreenRef{std::move(seq), LX_FIELD_NONE});
          }
        }
      }
    } else if (loop.items.size() > uast::kSeqThreshold) {
      std::vector<uast::GreenRef> raw = to_raw_refs(loop.items);
      uast::GreenPtr seq = uast::seq_from_items(
          raw, loop.ctx_depth, ctx_h, loop.pred_deps_hash, extra_reach);
      check_node_limit();
      if (!rf.loops.empty()) {
        flush_loop_items_to_seq(rf.loops.back());
        rf.loops.back().seq_acc =
            uast::seq_concat(std::move(rf.loops.back().seq_acc), std::move(seq));
      } else {
        rf.children.push_back(OwnedGreenRef{std::move(seq), LX_FIELD_NONE});
      }
    } else {
      for (auto& item : loop.items) {
        if (!rf.loops.empty()) {
          rf.loops.back().items.push_back(std::move(item));
        } else {
          rf.children.push_back(std::move(item));
        }
      }
    }

    rf.ctx_depth = std::max(rf.ctx_depth, loop.ctx_depth);
    rf.pred_deps_hash = combine_pred_hash(rf.pred_deps_hash, loop.pred_deps_hash);
    rf.max_la_end = std::max(rf.max_la_end, loop.max_la_end);
  }

  static bool is_loop_end_marker(const uast::GreenNode* n) noexcept {
    if (!n) return false;
    return n->is_error() || n->is_missing() ||
           (n->is_inner() && n->as_inner()->ctx_depth == UINT32_MAX) ||
           uast::kind_symbol(n->kind) == LX_SYMBOL_EOF;
  }

  uint32_t count_valid_loop_prefix_items(const uast::GreenNode* seq,
                                         const LoopFrame& loop) const noexcept {
    if (!seq) return 0;
    const uint16_t rule_hint =
        (loop.body_entry_symbol != LX_SYMBOL_INVALID)
            ? find_loop_entry_rule_hint(loop.block_start_state)
            : atn::kIndexNoneU16;
    uint32_t count = 0;
    std::vector<const uast::GreenNode*> stack;
    stack.push_back(seq);
    while (!stack.empty()) {
      const uast::GreenNode* n = stack.back();
      stack.pop_back();
      if (!n) continue;
      if (n->is_seq()) {
        const auto ch = n->as_inner()->children();
        for (size_t i = ch.size(); i > 0; --i) {
          if (ch[i - 1].node) stack.push_back(ch[i - 1].node);
        }
        continue;
      }
      if (is_loop_end_marker(n)) {
        return count;
      }
      if (loop.body_single_item && !n->is_hidden() &&
          loop.body_entry_symbol != LX_SYMBOL_INVALID &&
          !item_matches_loop_entry(n, rule_hint, loop.body_entry_symbol)) {
        return count;
      }
      ++count;
    }
    return count;
  }

  void try_fast_forward_loop(RuleFrame& rf, LoopFrame& loop) {
    if (!loop.spliceable || !oracle_ || oracle_->empty()) {
      return;
    }
    const LexToken& tok = peek(0);
    if (!tok.from_old) {
      return;
    }
    const uast::GreenNode* old_seq =
        oracle_->find_seq_starting_at(tok.old_offset, rf.node_kind);
    if (!old_seq || !oracle_->can_splice_seq(old_seq, caller_return_states_)) {
      return;
    }
    loop.old_seq = old_seq;
    loop.old_seq_start = tok.old_offset;

    uast::GreenPtr prefix = oracle_->fast_forward_seq(
        old_seq, tok.old_offset, caller_return_states_);
    if (!prefix) {
      return;
    }

    const uint32_t raw_prefix_count = uast::seq_count(prefix.get());
    uint32_t count = count_valid_loop_prefix_items(prefix.get(), loop);
    if (count == 0) {
      return;
    }
    const uint32_t total_old = uast::seq_count(old_seq);
    if (count < total_old && !loop.body_single_item &&
        loop.body_entry_symbol != LX_SYMBOL_INVALID) {
      auto stop_item = uast::seq_get(old_seq, count);
      const bool stopped_at_end_marker =
          stop_item && is_loop_end_marker(stop_item->node);
      if (!stopped_at_end_marker) {
        const uint16_t rule_hint = find_loop_entry_rule_hint(loop.block_start_state);
        while (count > 0) {
          auto next_item = uast::seq_get(old_seq, count);
          if (next_item &&
              item_matches_loop_entry(next_item->node, rule_hint,
                                      loop.body_entry_symbol)) {
            break;
          }
          --count;
        }
      }
      if (count == 0) {
        return;
      }
    }
    if (count < total_old) {
      while (count > 0) {
        auto tail_item = uast::seq_get(old_seq, count - 1);
        if (!tail_item || !tail_item->node->is_hidden()) {
          break;
        }
        --count;
      }
      if (count == 0) {
        return;
      }
    }
    if (count < raw_prefix_count) {
      prefix = uast::seq_split_at_index(old_seq, count).left;
      if (!prefix) return;
    }

    if (prefix->width == 0 && uast::seq_count(prefix.get()) == 0) {
      return;
    }

    const uint32_t state_out = ReuseOracle::last_token_lex_state_out(prefix.get());
    const uint32_t jump_to = tok.new_offset + prefix->width;
    loop.seq_acc = std::move(prefix);
    jump_stream(jump_to, state_out);
    ++stats_.seq_fast_forwards;
  }

  void try_adopt_suffix_loop(LoopFrame& loop) {
    if (!loop.spliceable || !loop.old_seq || !oracle_ || oracle_->empty()) {
      return;
    }
    if (!oracle_->can_splice_seq(loop.old_seq, caller_return_states_)) {
      return;
    }
    if (!stream_.synced() || !stream_.from_old()) {
      return;
    }
    const uint32_t old_off = stream_.old_offset();
    if (old_off < loop.old_seq_start ||
        old_off >= loop.old_seq_start + loop.old_seq->width) {
      return;
    }

    uast::GreenPtr suffix =
        oracle_->adopt_suffix_seq(loop.old_seq, loop.old_seq_start, old_off);
    if (!suffix) {
      return;
    }

    if (!loop.body_single_item && loop.body_entry_symbol != LX_SYMBOL_INVALID) {
      const uint16_t rule_hint = find_loop_entry_rule_hint(loop.block_start_state);
      auto first_item = uast::seq_get(suffix.get(), 0);
      if (!first_item ||
          !item_matches_loop_entry(first_item->node, rule_hint, loop.body_entry_symbol)) {
        return;
      }
    }

    const uint32_t raw_suffix_count = uast::seq_count(suffix.get());
    uint32_t valid_count = count_valid_loop_prefix_items(suffix.get(), loop);
    if (valid_count == 0) {
      return;
    }

    const uint32_t old_seq_end = loop.old_seq_start + loop.old_seq->width;
    const uint32_t suffix_valid_end =
        (valid_count == raw_suffix_count) ? (old_off + suffix->width) : old_seq_end;
    const bool stopped_early =
        (valid_count < raw_suffix_count) || (suffix_valid_end < old_seq_end);

    if (stopped_early && !loop.body_single_item &&
        loop.body_entry_symbol != LX_SYMBOL_INVALID) {
      std::optional<uast::GreenRef> stop_item;
      if (valid_count < raw_suffix_count) {
        stop_item = uast::seq_get(suffix.get(), valid_count);
      } else if (suffix_valid_end < old_seq_end) {
        const auto hit = uast::seq_find_by_byte(
            loop.old_seq, suffix_valid_end - loop.old_seq_start);
        if (hit) {
          stop_item = hit->ref;
        }
      }
      const bool stopped_at_end_marker =
          stop_item && is_loop_end_marker(stop_item->node);
      if (!stopped_at_end_marker) {
        const uint16_t rule_hint = find_loop_entry_rule_hint(loop.block_start_state);
        const bool already_at_entry =
            stop_item && item_matches_loop_entry(stop_item->node, rule_hint,
                                                 loop.body_entry_symbol);
        if (!already_at_entry) {
          while (valid_count > 0) {
            --valid_count;
            auto cand = uast::seq_get(suffix.get(), valid_count);
            if (cand &&
                item_matches_loop_entry(cand->node, rule_hint,
                                        loop.body_entry_symbol)) {
              break;
            }
          }
        }
      }
      if (valid_count == 0) {
        return;
      }
    }

    if (stopped_early) {
      while (valid_count > 0) {
        auto tail_item = uast::seq_get(suffix.get(), valid_count - 1);
        if (!tail_item || !tail_item->node->is_hidden()) {
          break;
        }
        --valid_count;
      }
      if (valid_count == 0) {
        return;
      }
    }
    if (valid_count < raw_suffix_count) {
      suffix = uast::seq_split_at_index(suffix.get(), valid_count).left;
      if (!suffix) return;
    }

    if (suffix->width == 0 && uast::seq_count(suffix.get()) == 0) {
      return;
    }

    // Flush any pending hidden trivia (re-lexed or synced) before the adopted suffix.
    flush_hidden_to_current();
    const uint32_t state_out = ReuseOracle::last_token_lex_state_out(suffix.get());
    const uint32_t jump_to = stream_.new_offset() + suffix->width;
    flush_loop_items_to_seq(loop);
    loop.seq_acc = uast::seq_concat(std::move(loop.seq_acc), std::move(suffix));
    jump_stream(jump_to, state_out);
    ++stats_.seq_suffix_adoptions;
  }

  void handle_star_loop_entry(RuleFrame& rf, const atn::AtnState& st) {
    if (st.flags & atn::kStateLeftRecEntry) {
      if (!rf.is_flattened && !rf.inside_prec_loop) {
        rf.lr_left = build_rule_node(rf, rf.node_kind, rf.node_flags, rf.children);
        rf.children.clear();
        const auto& r = atn_.rule(rf.rule_id);
        rf.node_kind = uast::make_kind(opts_.lang_idx, r.symbol_id);
        rf.node_flags = uast::FLAG_RULE;
        rf.inside_prec_loop = true;
      } else if (rf.inside_prec_loop &&
                 stream_.new_offset() == rf.lr_iter_start_byte &&
                 rf.lr_iter_start_byte != UINT32_MAX) {
        // Zero-progress guard in prec loop.
        rf.current_state = st.loop_end_state;
        return;
      }

      const auto trans = atn_.state_transitions(rf.current_state);
      auto outcome = internal::predict_decision(
          atn_, dfa_cache_, rf.current_state, rf.prec, caller_stack_, *this);
      record_predict_stats(outcome);
      propagate_la_and_ctx(outcome.used_ctx_depth);

      if (outcome.alt < 0 || static_cast<size_t>(outcome.alt) >= trans.size()) {
        rf.current_state = st.loop_end_state;
        return;
      }
      const auto& tr = trans[static_cast<size_t>(outcome.alt)];
      if (tr.to == st.loop_end_state) {
        rf.current_state = st.loop_end_state;
        return;
      }

      rf.lr_iter_start_byte = stream_.new_offset();
      bool found_alt_label = false;
      for (const auto& al : atn_.rule_alt_labels(rf.rule_id)) {
        if (al.entry_state == tr.to) {
          rf.lr_alt_kind = uast::make_kind(opts_.lang_idx, al.symbol_id);
          rf.lr_alt_flags = uast::FLAG_RULE | uast::FLAG_ALT_LABEL;
          found_alt_label = true;
          break;
        }
      }
      if (!found_alt_label) {
        rf.lr_alt_kind = uast::make_kind(opts_.lang_idx, atn_.rule(rf.rule_id).symbol_id);
        rf.lr_alt_flags = uast::FLAG_RULE;
      }
      rf.lr_lhs_field = LX_FIELD_NONE;
      rf.current_state = tr.to;
      return;
    }

    // Normal '*' loop.
    const bool entering_fresh =
        rf.loops.empty() || rf.loops.back().entry_state != rf.current_state;
    if (entering_fresh) {
      flush_hidden_to_current();
      LoopFrame loop;
      loop.entry_state = rf.current_state;
      loop.loop_end_state = st.loop_end_state;
      loop.spliceable = (st.flags & atn::kStateLoopSpliceable) != 0;
      loop.is_plus = false;
      for (const auto& tr : atn_.state_transitions(rf.current_state)) {
        if (tr.to != st.loop_end_state) {
          loop.block_start_state = tr.to;
          break;
        }
      }
      if (loop.block_start_state != atn::kIndexNoneU32) {
        analyze_loop_body(loop.block_start_state, loop.body_entry_symbol,
                          loop.body_single_item);
      }
      rf.loops.push_back(std::move(loop));
      try_fast_forward_loop(rf, rf.loops.back());
    } else {
      LoopFrame& loop = rf.loops.back();
      if (stream_.new_offset() == loop.iter_start_byte) {
        // Zero-progress loop iteration -> exit loop.
        finalize_and_pop_loop(rf);
        rf.current_state = st.loop_end_state;
        return;
      }
      try_adopt_suffix_loop(loop);
    }

    const auto trans = atn_.state_transitions(rf.current_state);
    auto outcome = internal::predict_decision(
        atn_, dfa_cache_, rf.current_state, rf.prec, caller_stack_, *this);
    record_predict_stats(outcome);
    propagate_la_and_ctx(outcome.used_ctx_depth);

    if (outcome.alt < 0 || static_cast<size_t>(outcome.alt) >= trans.size()) {
      finalize_and_pop_loop(rf);
      rf.current_state = st.loop_end_state;
      return;
    }

    const auto& tr = trans[static_cast<size_t>(outcome.alt)];
    if (tr.to == st.loop_end_state) {
      finalize_and_pop_loop(rf);
      rf.current_state = st.loop_end_state;
      return;
    }

    LoopFrame& loop = rf.loops.back();
    loop.iter_start_byte = stream_.new_offset();
    loop.iter_start_item_count = static_cast<uint32_t>(loop.items.size());
    rf.current_state = tr.to;
  }

  void handle_star_loop_back(RuleFrame& rf, const atn::AtnState& st) {
    if (rf.is_left_recursive && rf.inside_prec_loop && !rf.is_flattened) {
      std::vector<OwnedGreenRef> combined;
      combined.reserve(1 + rf.children.size());
      combined.push_back(OwnedGreenRef{std::move(rf.lr_left), rf.lr_lhs_field});
      for (auto& c : rf.children) {
        combined.push_back(std::move(c));
      }
      rf.lr_left = build_rule_node(rf, rf.lr_alt_kind, rf.lr_alt_flags, combined);
      rf.children.clear();
      rf.lr_lhs_field = LX_FIELD_NONE;
    }
    rf.current_state = st.pair_state;
  }

  bool handle_plus_block_start(RuleFrame& rf, const atn::AtnState& st) {
    const uint32_t loop_back_id =
        (rf.current_state < plus_start_to_loop_back_.size())
            ? plus_start_to_loop_back_[rf.current_state]
            : atn::kIndexNoneU32;
    if (loop_back_id == atn::kIndexNoneU32) {
      return false;
    }
    const bool entering_fresh =
        rf.loops.empty() || rf.loops.back().entry_state != loop_back_id;
    if (!entering_fresh) {
      rf.loops.back().iter_start_byte = stream_.new_offset();
      rf.loops.back().iter_start_item_count =
          static_cast<uint32_t>(rf.loops.back().items.size());
      return false;
    }

    flush_hidden_to_current();
    const auto& lb = atn_.state(loop_back_id);
    LoopFrame loop;
    loop.entry_state = loop_back_id;
    loop.block_start_state = rf.current_state;
    loop.loop_end_state = lb.loop_end_state;
    loop.spliceable = (lb.flags & atn::kStateLoopSpliceable) != 0;
    loop.is_plus = true;
    analyze_loop_body(rf.current_state, loop.body_entry_symbol, loop.body_single_item);
    rf.loops.push_back(std::move(loop));

    try_fast_forward_loop(rf, rf.loops.back());
    if (rf.loops.back().seq_acc) {
      // At least 1 iteration already fast-forwarded -> jump to PLUS_LOOP_BACK.
      rf.loops.back().iter_start_byte = UINT32_MAX;
      rf.current_state = loop_back_id;
      return true;
    }
    rf.loops.back().iter_start_byte = stream_.new_offset();
    rf.loops.back().iter_start_item_count = 0;
    (void)st;
    return false;
  }

  void handle_plus_loop_back(RuleFrame& rf, const atn::AtnState& st) {
    if (!rf.loops.empty() && rf.loops.back().entry_state == rf.current_state) {
      LoopFrame& loop = rf.loops.back();
      if (loop.iter_start_byte != UINT32_MAX &&
          stream_.new_offset() == loop.iter_start_byte) {
        finalize_and_pop_loop(rf);
        rf.current_state = st.loop_end_state;
        return;
      }
      try_adopt_suffix_loop(loop);
    }

    const auto trans = atn_.state_transitions(rf.current_state);
    auto outcome = internal::predict_decision(
        atn_, dfa_cache_, rf.current_state, rf.prec, caller_stack_, *this);
    record_predict_stats(outcome);
    propagate_la_and_ctx(outcome.used_ctx_depth);

    if (outcome.alt < 0 || static_cast<size_t>(outcome.alt) >= trans.size()) {
      if (!rf.loops.empty() && rf.loops.back().entry_state == rf.current_state) {
        finalize_and_pop_loop(rf);
      }
      rf.current_state = st.loop_end_state;
      return;
    }

    const auto& tr = trans[static_cast<size_t>(outcome.alt)];
    if (tr.to == st.loop_end_state) {
      if (!rf.loops.empty() && rf.loops.back().entry_state == rf.current_state) {
        finalize_and_pop_loop(rf);
      }
      rf.current_state = st.loop_end_state;
      return;
    }

    if (!rf.loops.empty() && rf.loops.back().entry_state == rf.current_state) {
      rf.loops.back().iter_start_byte = stream_.new_offset();
      rf.loops.back().iter_start_item_count =
          static_cast<uint32_t>(rf.loops.back().items.size());
    }
    rf.current_state = tr.to;
  }

  void handle_loop_end(RuleFrame& rf, const atn::AtnState& st) {
    if (!rf.loops.empty() && rf.loops.back().loop_end_state == rf.current_state) {
      finalize_and_pop_loop(rf);
    }
    const auto trans = atn_.state_transitions(rf.current_state);
    if (trans.empty()) {
      rf.current_state = atn_.rule(rf.rule_id).stop_state;
      return;
    }
    (void)st;
    execute_transition(rf, trans[0]);
  }

  void record_predict_stats(const internal::PredictOutcome& o) noexcept {
    if (o.used_ll1) ++stats_.ll1_decisions;
    if (o.used_llk) ++stats_.llk_decisions;
    if (o.used_sll_hit) ++stats_.sll_dfa_hits;
    if (o.used_sll_miss) ++stats_.sll_dfa_misses;
    if (o.used_full_ll) ++stats_.full_ll_decisions;
  }

  void apply_alt_label_if_entry(RuleFrame& rf, uint32_t next_state) noexcept {
    if (rf.inside_prec_loop || rf.is_flattened) return;
    if (atn_.rule(rf.rule_id).alt_label_count == 0) return;
    for (const auto& al : atn_.rule_alt_labels(rf.rule_id)) {
      if (al.entry_state == next_state) {
        rf.node_kind = uast::make_kind(opts_.lang_idx, al.symbol_id);
        rf.node_flags |= uast::FLAG_ALT_LABEL;
        return;
      }
    }
  }

  void handle_decision(RuleFrame& rf,
                       const atn::AtnState& st,
                       std::span<const atn::AtnTransition> trans) {
    auto outcome = internal::predict_decision(
        atn_, dfa_cache_, rf.current_state, rf.prec, caller_stack_, *this);
    record_predict_stats(outcome);
    propagate_la_and_ctx(outcome.used_ctx_depth);

    if (outcome.alt >= 0 && static_cast<size_t>(outcome.alt) < trans.size()) {
      const auto& chosen = trans[static_cast<size_t>(outcome.alt)];
      apply_alt_label_if_entry(rf, chosen.to);
      execute_transition(rf, chosen);
      return;
    }

    // Check if an optional exit transition to BLOCK_END exists (e.g. (a)?).
    if (st.kind == atn::StateKind::BlockStart &&
        st.pair_state != atn::kIndexNoneU32) {
      for (const auto& tr : trans) {
        if (tr.to == st.pair_state) {
          rf.current_state = tr.to;
          return;
        }
      }
    }

    recover_decision_failure(rf, st, trans);
  }

  void execute_transition(RuleFrame& rf, const atn::AtnTransition& tr) {
    switch (tr.kind) {
      case atn::TransitionKind::Epsilon:
      case atn::TransitionKind::Action:
        apply_alt_label_if_entry(rf, tr.to);
        rf.current_state = tr.to;
        return;

      case atn::TransitionKind::Precedence:
        if (tr.arg0 >= rf.prec) {
          if (tr.field_id != LX_FIELD_NONE) {
            rf.lr_lhs_field = tr.field_id;
          }
          rf.current_state = tr.to;
        } else {
          // Precedence guard failed -> exit rule / loop safely.
          rf.current_state = atn_.rule(rf.rule_id).stop_state;
        }
        return;

      case atn::TransitionKind::Pred:
        if (eval_predicate(tr.arg0, tr.arg1, 0u) != 0) {
          rf.current_state = tr.to;
        } else {
          recover_decision_failure(rf, atn_.state(rf.current_state),
                                   std::span<const atn::AtnTransition>(&tr, 1));
        }
        return;

      case atn::TransitionKind::Rule: {
        const uint16_t callee = static_cast<uint16_t>(tr.arg0);
        const uint16_t callee_prec =
            (tr.flags & atn::kTransPrecedenceArg) ? static_cast<uint16_t>(tr.arg1) : 0u;
        enter_rule(rf, callee, callee_prec, tr.field_id, tr.to);
        return;
      }

      case atn::TransitionKind::Atom:
      case atn::TransitionKind::Set:
      case atn::TransitionKind::NotSet:
      case atn::TransitionKind::Range:
      case atn::TransitionKind::Wildcard: {
        const LexToken& tok = peek(0);
        if (atn_.transition_matches(tr, tok.symbol)) {
          flush_hidden_to_current();
          LexToken consumed = consume_next_token();
          uast::GreenPtr g = ensure_token_green(consumed, opts_.lang_idx,
                                                opts_.token_interner);
          append_child_to_current(std::move(g), tr.field_id);
          rf.current_state = tr.to;
        } else {
          recover_terminal_mismatch(rf, tr);
        }
        return;
      }
    }
  }

  void enter_rule(RuleFrame& caller,
                  uint16_t callee_rule_id,
                  uint16_t callee_prec,
                  lx_field field_id,
                  uint32_t return_state) {
    flush_hidden_to_current();
    if (truncated_) return;

    if (oracle_ && !oracle_->empty()) {
      const LexToken& tok = peek(0);
      if (tok.from_old && callee_rule_id < rule_accepted_kinds_.size()) {
        caller_stack_.push_back(internal::ActiveCallerFrame{return_state, caller.prec});
        caller_return_states_.push_back(return_state);
        const auto cand = oracle_->find_reusable_rule_any(
            tok.old_offset,
            rule_accepted_kinds_[callee_rule_id],
            callee_prec,
            caller_return_states_);
        caller_stack_.pop_back();
        caller_return_states_.pop_back();

        const auto* first = cand ? uast::first_token(cand->node) : nullptr;
        if (cand && first && first->lex_state_in == tok.lex_state_in) {
          jump_stream(tok.new_offset + cand->node->width, cand->last_lex_state_out);
          append_child_to_current(uast::GreenPtr(cand->node), field_id);
          caller.current_state = return_state;
          ++stats_.nodes_reused;
          return;
        }
      }
    }

    push_rule_frame(callee_rule_id, callee_prec, field_id, return_state);
  }

  bool bump_error_count() {
    ++stats_.error_recoveries;
    ++error_count_;
    if (opts_.limits.max_errors > 0 && error_count_ > opts_.limits.max_errors) {
      trigger_truncation(DiagnosticCode::MaxErrors, "max error limit exceeded");
      return false;
    }
    return true;
  }

  bool in_active_sync_set(lx_symbol sym,
                          size_t& out_matching_frame,
                          uint32_t* out_used_ctx_depth = nullptr) const noexcept {
    if (sym == LX_SYMBOL_EOF) {
      out_matching_frame = 0;
      if (out_used_ctx_depth) {
        *out_used_ctx_depth =
            (frames_.size() > 1) ? static_cast<uint32_t>(frames_.size() - 1) : 0u;
      }
      return true;
    }
    for (size_t i = frames_.size(); i > 0; --i) {
      const RuleFrame& f = frames_[i - 1];
      const uint16_t sync_id = atn_.effective_sync_set(f.current_state);
      if (internal::symbol_in_sync_set(atn_, sync_id, sym)) {
        out_matching_frame = i - 1;
        if (out_used_ctx_depth) {
          *out_used_ctx_depth = static_cast<uint32_t>(frames_.size() - i);
        }
        return true;
      }
    }
    if (out_used_ctx_depth) {
      *out_used_ctx_depth =
          (frames_.size() > 1) ? static_cast<uint32_t>(frames_.size() - 1) : 0u;
    }
    return false;
  }

  void recover_terminal_mismatch(RuleFrame& rf, const atn::AtnTransition& tr) {
    if (!bump_error_count()) return;

    const LexToken& cur = peek(0);

    // 0) Lexical error token: consume it as an Error leaf/node so we never stall.
    if (cur.is_error) {
      flush_hidden_to_current();
      LexToken err_tok = consume_next_token();
      uast::GreenPtr g = ensure_token_green(err_tok, opts_.lang_idx, opts_.token_interner);
      append_child_to_current(std::move(g), LX_FIELD_NONE);
      return;
    }

    // 1) Single-token insertion (Missing, §6.5(a)).
    if (tr.kind == atn::TransitionKind::Atom &&
        tr.arg0 >= LX_FIRST_USER_SYMBOL &&
        tr.arg0 < atn_.terminal_count()) {
      const lx_symbol exp_sym = static_cast<lx_symbol>(tr.arg0);
      uint32_t reach_ctx_depth = 0;
      const bool can_ins = internal::can_reach_symbol_in_one_step(
          atn_, tr.to, rf.prec, caller_stack_, cur.symbol, &reach_ctx_depth);
      propagate_la_and_ctx(reach_ctx_depth);
      if (can_ins) {
        const uint32_t pos = consumed_end_byte_;
        const uint32_t reach =
            (rf.max_la_end > pos) ? static_cast<uint32_t>(rf.max_la_end - pos) : 0u;
        const uint32_t st_id = stream_.state_id();
        uast::GreenPtr missing = uast::make_missing(
            uast::make_kind(opts_.lang_idx, exp_sym), reach, st_id, st_id);
        append_child_to_current(std::move(missing), tr.field_id);
        rf.current_state = tr.to;
        return;
      }
    }

    // 2) Single-token deletion (Unexpected token, §6.5(b)).
    if (!cur.is_eof()) {
      const LexToken& next1 = peek(1);
      if (atn_.transition_matches(tr, next1.symbol)) {
        flush_hidden_to_current();
        LexToken bad = consume_next_token();
        uast::GreenPtr bad_g = ensure_token_green(bad, opts_.lang_idx, opts_.token_interner);
        const uint32_t end_pos = consumed_end_byte_;
        const uint32_t extra =
            (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
        const uast::GreenRef ref{bad_g.get(), uast::kNoField};
        uast::GreenPtr err_node = uast::make_error(
            uast::make_kind(opts_.lang_idx, LX_SYMBOL_ERROR),
            std::span<const uast::GreenRef>(&ref, 1), extra);
        append_child_to_current(std::move(err_node), LX_FIELD_NONE);
        return; // Retry transition on next1!
      }
    }

    // 3) Panic mode (§6.5(c)).
    panic_skip_to_sync(rf, &tr);
  }

  void recover_decision_failure(RuleFrame& rf,
                                const atn::AtnState& st,
                                std::span<const atn::AtnTransition> trans) {
    if (!bump_error_count()) return;

    const LexToken& cur = peek(0);
    if (cur.is_error) {
      flush_hidden_to_current();
      LexToken err_tok = consume_next_token();
      uast::GreenPtr g = ensure_token_green(err_tok, opts_.lang_idx, opts_.token_interner);
      append_child_to_current(std::move(g), LX_FIELD_NONE);
      return;
    }

    // Check if skipping one token resolves the decision on la(1) (§6.5(b)).
    if (!cur.is_eof() && trans.size() > 1) {
      struct ShiftedLa final : internal::LookaheadAdapter {
        ParserSession& self;
        explicit ShiftedLa(ParserSession& s) : self(s) {}
        const LexToken& peek(uint32_t k) override { return self.peek(k + 1); }
        int eval_predicate(uint32_t pred_id, uint32_t pred_flags, uint32_t k_off) override {
          return self.eval_predicate(pred_id, pred_flags, k_off + 1);
        }
        bool is_cancelled() const noexcept override { return self.is_cancelled(); }
      } shifted(*this);

      auto look_outcome = internal::predict_decision(
          atn_, dfa_cache_, rf.current_state, rf.prec, caller_stack_, shifted);
      propagate_la_and_ctx(look_outcome.used_ctx_depth);
      if (look_outcome.alt >= 0) {
        flush_hidden_to_current();
        LexToken bad = consume_next_token();
        uast::GreenPtr bad_g = ensure_token_green(bad, opts_.lang_idx, opts_.token_interner);
        const uint32_t end_pos = consumed_end_byte_;
        const uint32_t extra =
            (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
        const uast::GreenRef ref{bad_g.get(), uast::kNoField};
        uast::GreenPtr err_node = uast::make_error(
            uast::make_kind(opts_.lang_idx, LX_SYMBOL_ERROR),
            std::span<const uast::GreenRef>(&ref, 1), extra);
        append_child_to_current(std::move(err_node), LX_FIELD_NONE);
        return;
      }
    }

    (void)st;
    panic_skip_to_sync(rf, nullptr);
  }

  void panic_skip_to_sync(RuleFrame& rf, const atn::AtnTransition* expected_tr) {
    std::vector<OwnedGreenRef> err_tokens;
    const uint32_t max_toks = std::max<uint32_t>(1u, opts_.limits.max_error_tokens);
    size_t sync_frame_idx = frames_.size() - 1;

    while (err_tokens.size() < max_toks) {
      const LexToken& tok = peek(0);
      if (tok.is_eof()) {
        sync_frame_idx = 0;
        if (frames_.size() > 1) {
          propagate_la_and_ctx(static_cast<uint32_t>(frames_.size() - 1));
        }
        break;
      }
      if (!err_tokens.empty()) {
        if (expected_tr && atn_.transition_matches(*expected_tr, tok.symbol)) {
          break;
        }
        uint32_t sync_ctx_depth = 0;
        const bool synced =
            in_active_sync_set(tok.symbol, sync_frame_idx, &sync_ctx_depth);
        propagate_la_and_ctx(sync_ctx_depth);
        if (synced) {
          break;
        }
      }
      if (err_tokens.empty()) {
        flush_hidden_to_current();
      } else {
        for (const LexToken& ht : stream_.take_hidden()) {
          consumed_end_byte_ = ht.new_offset + ht.width;
          uast::GreenPtr hg = ensure_token_green(ht, opts_.lang_idx, opts_.token_interner);
          err_tokens.push_back(OwnedGreenRef{std::move(hg), LX_FIELD_NONE});
        }
      }
      LexToken consumed = consume_next_token();
      uast::GreenPtr g = ensure_token_green(consumed, opts_.lang_idx, opts_.token_interner);
      err_tokens.push_back(OwnedGreenRef{std::move(g), LX_FIELD_NONE});
    }

    if (!err_tokens.empty()) {
      const uint32_t end_pos = consumed_end_byte_;
      const uint32_t extra =
          (rf.max_la_end > end_pos) ? static_cast<uint32_t>(rf.max_la_end - end_pos) : 0u;
      std::vector<uast::GreenRef> raw = to_raw_refs(err_tokens);
      uast::GreenPtr err_node = uast::make_error(
          uast::make_kind(opts_.lang_idx, LX_SYMBOL_ERROR), raw, extra);
      append_child_to_current(std::move(err_node), LX_FIELD_NONE);
    }

    const LexToken& after = peek(0);
    if (expected_tr && atn_.transition_matches(*expected_tr, after.symbol)) {
      return; // Retry expected_tr in current frame.
    }

    // If synchronized on an enclosing frame (or EOF), unwind inner frames to it.
    uint32_t after_sync_ctx_depth = 0;
    const bool after_synced =
        in_active_sync_set(after.symbol, sync_frame_idx, &after_sync_ctx_depth);
    propagate_la_and_ctx(after_sync_ctx_depth);
    if (after_synced && sync_frame_idx < frames_.size() - 1) {
      while (frames_.size() - 1 > sync_frame_idx) {
        RuleFrame& inner = frames_.back();
        // Mark active loops and the unwound frame itself as non-reusable:
        // inter-frame panic-unwind skips inner's normal return path in caller.
        for (auto& lp : inner.loops) {
          lp.ctx_depth = UINT32_MAX;
        }
        inner.ctx_depth = UINT32_MAX;
        while (!inner.loops.empty()) {
          finalize_and_pop_loop(inner);
        }
        uast::GreenPtr node = finalize_rule_frame_node(inner, 0u);
        const uint32_t ret = inner.return_state;
        const lx_field fld = inner.target_field;
        pop_rule_frame();
        append_child_to_current(std::move(node), fld);
        frames_.back().current_state = ret;
      }
      return;
    }

    // Otherwise exit current rule cleanly so caller can proceed.
    frames_.back().current_state = atn_.rule(frames_.back().rule_id).stop_state;
  }

  void unwind_truncated() {
    while (!frames_.empty()) {
      RuleFrame& rf = frames_.back();
      while (!rf.loops.empty()) {
        finalize_and_pop_loop(rf);
      }
      uast::GreenPtr node = finalize_rule_frame_node(rf, uast::FLAG_TRUNCATED);
      const lx_field fld = rf.target_field;
      pop_rule_frame();
      if (frames_.empty()) {
        root_ = std::move(node);
      } else {
        append_child_to_current(std::move(node), fld);
      }
    }
  }

  const ParseOptions& opts_;
  const atn::Atn& atn_;
  DfaCache* dfa_cache_ = nullptr;
  TokenStream& stream_;
  const ReuseOracle* oracle_ = nullptr;
  std::chrono::steady_clock::time_point start_time_;

  std::vector<uint32_t> plus_start_to_loop_back_;
  std::vector<std::vector<uast::Kind>> rule_accepted_kinds_;

  std::vector<RuleFrame> frames_;
  std::vector<internal::ActiveCallerFrame> caller_stack_;
  std::vector<uint32_t> caller_return_states_;

  std::unordered_map<uint32_t, uint64_t> env_dep_seen_;
  std::vector<std::pair<uint32_t, uint64_t>> env_deps_snapshot_;

  uast::GreenPtr root_;
  ParseStats stats_{};
  uint32_t consumed_end_byte_ = 0;
  uint32_t error_count_ = 0;
  bool truncated_ = false;
};

} // namespace

// ============================================================================
// ParseDriver & Free Functions
// ============================================================================

ParseDriver::ParseDriver(ParseOptions opts) : opts_(opts) {
  if (!opts_.state_interner) {
    owned_state_interner_ = std::make_unique<StateInterner>();
    opts_.state_interner = owned_state_interner_.get();
  }
  if (!opts_.dfa_cache) {
    owned_dfa_cache_ = std::make_unique<DfaCache>(opts_.atn);
    opts_.dfa_cache = owned_dfa_cache_.get();
  }
}

ParseDriver::~ParseDriver() = default;

ParseResult ParseDriver::parse_full(const Rope& rope) {
  return run_parse(&rope, nullptr, nullptr, nullptr);
}

ParseResult ParseDriver::parse_full(const RopeView& view) {
  return run_parse(nullptr, &view, nullptr, nullptr);
}

ParseResult ParseDriver::reparse(const OldSnapshotView& old_snapshot,
                                 const ChangeSet& changes,
                                 const Rope& new_rope) {
  return run_parse(&new_rope, nullptr, &old_snapshot, &changes);
}

ParseResult ParseDriver::reparse(const OldSnapshotView& old_snapshot,
                                 const ChangeSet& changes,
                                 const RopeView& new_view) {
  return run_parse(nullptr, &new_view, &old_snapshot, &changes);
}

ParseResult ParseDriver::run_parse(const Rope* rope,
                                   const RopeView* view,
                                   const OldSnapshotView* old_snapshot,
                                   const ChangeSet* changes) {
  const auto t0 = std::chrono::steady_clock::now();
  ParseResult res;

  if (!opts_.lang || !opts_.atn || (!rope && !view)) {
    res.status = LX_BAD_ARG;
    return res;
  }

  const atn::Atn& atn = *opts_.atn;
  const uint16_t start_rule =
      (opts_.start_rule_override != atn::kIndexNoneU16)
          ? opts_.start_rule_override
          : atn.start_rule();
  if (start_rule >= atn.rules().size()) {
    res.status = LX_BAD_ARG;
    return res;
  }

  const uint32_t text_bytes = rope ? rope->byte_length() : view->byte_length();
  if (opts_.limits.max_file_bytes > 0 && text_bytes > opts_.limits.max_file_bytes) {
    const lx_symbol start_sym = atn.rule(start_rule).symbol_id;
    res.root = uast::make_inner(
        uast::make_kind(opts_.lang_idx, start_sym),
        uast::FLAG_RULE | uast::FLAG_TRUNCATED,
        {});
    res.truncated = true;
    res.status = LX_OK;
    Diagnostic d;
    d.code = DiagnosticCode::MaxFileBytes;
    d.severity = DiagnosticSeverity::Error;
    d.range = uast::Range{0u, 0u};
    d.message = "file exceeds max_file_bytes limit";
    res.diagnostics.push_back(std::move(d));
    return res;
  }

  const lx_language_info* lang_info =
      opts_.lang->info ? opts_.lang->info() : nullptr;
  const uint64_t module_grammar_hash =
      opts_.expected_grammar_hash != 0
          ? opts_.expected_grammar_hash
          : (lang_info ? lang_info->grammar_hash : 0ULL);

  bool use_incremental = false;
  bool env_dirty = false;
  if (old_snapshot && old_snapshot->root && changes) {
    const bool hash_ok =
        (module_grammar_hash == 0) ||
        (old_snapshot->grammar_hash == module_grammar_hash);
    const double ratio = changes->change_ratio(old_snapshot->root->width);
    if (hash_ok && ratio <= opts_.limits.full_reparse_ratio) {
      use_incremental = true;
      for (const auto& [dep_id, old_val] : old_snapshot->env_deps) {
        if (opts_.env.get(dep_id) != old_val) {
          env_dirty = true;
          break;
        }
      }
    }
  }

  // Fast-path when changes are empty and environment/grammar match.
  if (use_incremental && changes->empty() && !env_dirty &&
      opts_.start_rule_override == atn::kIndexNoneU16) {
    res.status = LX_OK;
    res.root = uast::GreenPtr(old_snapshot->root);
    res.used_incremental = true;
    res.truncated = res.root->is_truncated();
    res.diagnostics = extract_diagnostics(res.root.get(), lang_info);
    res.env_deps_snapshot.assign(old_snapshot->env_deps.begin(),
                                 old_snapshot->env_deps.end());
    const auto t1 = std::chrono::steady_clock::now();
    res.stats.elapsed_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    return res;
  }

  std::vector<uast::DirtySpan> dirty_spans;
  ReuseOracle oracle;
  if (use_incremental) {
    const auto hunks = changes->hunks();
    dirty_spans.reserve(hunks.size());
    for (const auto& h : hunks) {
      dirty_spans.push_back(uast::DirtySpan{h.old_start, h.old_end});
    }
    oracle.reset(old_snapshot->root, dirty_spans,
                 opts_.limits.error_heal_tokens, env_dirty);
  }

  TokenStreamOptions ts_opts;
  ts_opts.lang = opts_.lang;
  ts_opts.lang_idx = opts_.lang_idx;
  ts_opts.state_interner = opts_.state_interner;
  ts_opts.token_interner = opts_.token_interner;
  ts_opts.new_rope = rope;
  ts_opts.new_view = view;
  ts_opts.old_tree = use_incremental ? old_snapshot->root : nullptr;
  ts_opts.changes = use_incremental ? changes : nullptr;
  ts_opts.start_offset = 0;
  ts_opts.initial_state = opts_.initial_lex_state;

  TokenStream stream(ts_opts);
  ParserSession session(opts_, atn, opts_.dfa_cache,
                        stream, use_incremental ? &oracle : nullptr);

  res = session.execute(start_rule);
  res.used_incremental = use_incremental;

  if (res.status == LX_OK) {
    res.diagnostics = extract_diagnostics(res.root.get(), lang_info);
    for (auto& d : session.extra_limit_diagnostics) {
      res.diagnostics.push_back(std::move(d));
    }
    if (opts_.compute_diff && old_snapshot && old_snapshot->root && res.root) {
      res.changed_ranges = uast::tree_diff(old_snapshot->root, res.root.get());
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  res.stats.elapsed_us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
  return res;
}

ParseResult parse_full(const ParseOptions& opts, const Rope& rope) {
  ParseDriver driver(opts);
  return driver.parse_full(rope);
}

ParseResult parse_full(const ParseOptions& opts, const RopeView& view) {
  ParseDriver driver(opts);
  return driver.parse_full(view);
}

ParseResult reparse(const ParseOptions& opts,
                    const OldSnapshotView& old_snapshot,
                    const ChangeSet& changes,
                    const Rope& new_rope) {
  ParseDriver driver(opts);
  return driver.reparse(old_snapshot, changes, new_rope);
}

ParseResult reparse(const ParseOptions& opts,
                    const OldSnapshotView& old_snapshot,
                    const ChangeSet& changes,
                    const RopeView& new_view) {
  ParseDriver driver(opts);
  return driver.reparse(old_snapshot, changes, new_view);
}

} // namespace lx::parse
