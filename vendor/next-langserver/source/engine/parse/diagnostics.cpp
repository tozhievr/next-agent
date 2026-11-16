#include "engine/parse/parse.h"

#include <algorithm>
#include <array>

#include "engine/common/hash.h"

namespace lx::parse {

namespace {

std::string format_symbol_name(lx_symbol sym, const lx_language_info* info) {
  if (info && info->symbols && sym < info->symbol_count) {
    const lx_str& s = info->symbols[sym].name;
    if (s.ptr && s.len > 0) {
      return std::string(s.ptr, s.len);
    }
  }
  return "symbol#" + std::to_string(sym);
}

struct DiagFrame {
  const uast::GreenNode* node = nullptr;
  uint32_t offset = 0;
};

} // namespace

std::vector<Diagnostic> extract_diagnostics(const uast::GreenNode* root,
                                            const lx_language_info* info) {
  std::vector<Diagnostic> out;
  if (!root) {
    return out;
  }

  std::vector<DiagFrame> stack;
  stack.reserve(64);
  stack.push_back(DiagFrame{root, 0u});

  while (!stack.empty()) {
    const DiagFrame cur = stack.back();
    stack.pop_back();

    const uast::GreenNode* node = cur.node;
    if (!node) {
      continue;
    }

    const uint32_t flags = node->flags;
    if ((flags & (uast::FLAG_ERROR | uast::FLAG_MISSING | uast::FLAG_HAS_ERROR)) == 0) {
      continue;
    }

    if (node->is_missing()) {
      const lx_symbol exp_sym = uast::kind_symbol(node->kind);
      Diagnostic d;
      d.code = DiagnosticCode::MissingToken;
      d.severity = DiagnosticSeverity::Error;
      d.range = uast::Range{cur.offset, cur.offset};
      d.expected_symbol = exp_sym;
      d.actual_symbol = LX_SYMBOL_INVALID;
      d.message = "missing " + format_symbol_name(exp_sym, info);
      out.push_back(std::move(d));
      continue;
    }

    if (node->is_error()) {
      const lx_symbol sym = uast::kind_symbol(node->kind);
      const uast::GreenToken* first_tok = uast::first_token(node);
      const lx_symbol actual_sym = first_tok ? uast::kind_symbol(first_tok->kind) : sym;

      Diagnostic d;
      if (node->is_token() ||
          (sym == LX_SYMBOL_ERROR && node->child_count == 1 &&
           first_tok && uast::kind_symbol(first_tok->kind) == LX_SYMBOL_ERROR)) {
        d.code = DiagnosticCode::LexError;
        d.message = "lexical error";
      } else {
        d.code = DiagnosticCode::UnexpectedToken;
        d.message = "unexpected " + format_symbol_name(actual_sym, info);
      }
      d.severity = DiagnosticSeverity::Error;
      d.range = uast::Range{cur.offset, cur.offset + node->width};
      d.expected_symbol = LX_SYMBOL_INVALID;
      d.actual_symbol = actual_sym;
      out.push_back(std::move(d));
      continue;
    }

    if (node->is_inner()) {
      const auto* inner = node->as_inner();
      const auto children = inner->children();
      // Compute child offsets forward, then push in reverse for left-to-right order.
      uint32_t child_off = cur.offset;
      const size_t base_idx = stack.size();
      stack.resize(base_idx + children.size());
      for (size_t i = 0; i < children.size(); ++i) {
        const uast::GreenNode* ch = children[i].node;
        stack[base_idx + children.size() - 1 - i] = DiagFrame{ch, child_off};
        if (ch) {
          child_off += ch->width;
        }
      }
    }
  }

  return out;
}

uint32_t compute_ctx_hash(std::span<const uint32_t> state_stack,
                          uint32_t depth) noexcept {
  if (depth == 0 || state_stack.empty()) {
    return 0u;
  }
  const size_t n = state_stack.size();
  const size_t count = std::min<size_t>(depth, n);
  const size_t start = n - count;
  uint32_t h = lx::kFnv1a32OffsetBasis;
  for (size_t i = start; i < n; ++i) {
    h = lx::hash_combine_32(h, state_stack[i]);
  }
  return h;
}

// ============================================================================
// ReuseOracle
// ============================================================================

ReuseOracle::ReuseOracle(const uast::GreenNode* old_root,
                         std::span<const uast::DirtySpan> dirty_spans,
                         uint32_t error_heal_tokens,
                         bool env_dirty) {
  reset(old_root, dirty_spans, error_heal_tokens, env_dirty);
}

void ReuseOracle::reset(const uast::GreenNode* old_root,
                        std::span<const uast::DirtySpan> dirty_spans,
                        uint32_t error_heal_tokens,
                        bool env_dirty) {
  old_root_ = old_root;
  dirty_spans_.assign(dirty_spans.begin(), dirty_spans.end());
  std::sort(dirty_spans_.begin(), dirty_spans_.end(),
            [](const uast::DirtySpan& a, const uast::DirtySpan& b) {
              return (a.start < b.start) || (a.start == b.start && a.end < b.end);
            });
  error_heal_tokens_ = error_heal_tokens;
  env_dirty_ = env_dirty;
  cursor_ = uast::Cursor(old_root_);
}

uint32_t ReuseOracle::last_token_lex_state_out(
    const uast::GreenNode* node) noexcept {
  const uast::GreenToken* tok = uast::last_token(node);
  return tok ? tok->lex_state_out : 0u;
}

bool ReuseOracle::is_near_dirty_error(const uast::GreenNode* node,
                                      uint32_t old_start) const {
  if (!node || !node->has_error() || dirty_spans_.empty() || !old_root_) {
    return false;
  }

  const uint32_t node_end = old_start + node->width;
  uint32_t left_bound = old_start;
  uint32_t right_bound = node_end + node->reach_ahead;

  if (error_heal_tokens_ > 0) {
    uast::Cursor c(old_root_);
    if (c.goto_byte(old_start)) {
      // Walk backward to the start of the token at `old_start` first if inside.
      while (c.range().start > old_start && c.prev_leaf()) {
      }
      uint32_t steps = 0;
      while (steps < error_heal_tokens_ && c.prev_leaf()) {
        left_bound = std::min(left_bound, c.range().start);
        ++steps;
      }
      if (steps < error_heal_tokens_) {
        left_bound = 0;
      }
    } else {
      left_bound = 0;
    }

    uast::Cursor c_right(old_root_);
    if (node_end < old_root_->width && c_right.goto_byte(node_end)) {
      uint32_t steps = 0;
      while (steps < error_heal_tokens_) {
        const auto r = c_right.range();
        right_bound = std::max(right_bound, r.end + c_right.reach_ahead());
        ++steps;
        if (!c_right.next_leaf()) {
          right_bound = std::max(right_bound, old_root_->width + old_root_->reach_ahead);
          break;
        }
      }
    } else {
      right_bound = std::max(right_bound, old_root_->width + old_root_->reach_ahead);
    }
  }

  for (const auto& d : dirty_spans_) {
    if (d.start > right_bound) {
      break;
    }
    if (d.start == d.end) {
      if (d.start >= left_bound && d.start <= right_bound) {
        return true;
      }
    } else if (d.start < right_bound && d.end > left_bound) {
      return true;
    }
  }
  return false;
}

bool ReuseOracle::can_reuse(const uast::GreenNode* node,
                            uint32_t old_start,
                            uint32_t expected_prec,
                            std::span<const uint32_t> active_ctx_states) const {
  if (!node || !node->is_rule()) {
    return false;
  }
  const auto* inner = node->as_inner();
  if (!inner) {
    return false;
  }
  // R1: prec check
  if (inner->prec != expected_prec) {
    return false;
  }
  // R2: reach cleanliness
  if (!uast::is_node_clean(node, old_start, dirty_spans_)) {
    return false;
  }
  // R3: prediction context match (UINT32_MAX marks non-reusable inter-frame unwinds)
  if (inner->ctx_depth == UINT32_MAX) {
    return false;
  }
  if (inner->ctx_depth > 0) {
    if (inner->ctx_depth > active_ctx_states.size()) {
      return false;
    }
    if (compute_ctx_hash(active_ctx_states, inner->ctx_depth) != inner->ctx_hash) {
      return false;
    }
  }
  // R4: predicate dependency environment match
  if (env_dirty_ && inner->pred_deps_hash != 0) {
    return false;
  }
  // Error-healing guard (§6.3)
  if (is_near_dirty_error(node, old_start)) {
    return false;
  }
  return true;
}

std::optional<ReuseOracle::RuleCandidate> ReuseOracle::find_reusable_rule(
    uint32_t old_offset,
    uast::Kind expected_kind,
    uint32_t expected_prec,
    std::span<const uint32_t> active_ctx_states) const {
  const std::array<uast::Kind, 1> single{expected_kind};
  return find_reusable_rule_any(old_offset, single, expected_prec, active_ctx_states);
}

std::optional<ReuseOracle::RuleCandidate> ReuseOracle::find_reusable_rule_any(
    uint32_t old_offset,
    std::span<const uast::Kind> accepted_kinds,
    uint32_t expected_prec,
    std::span<const uint32_t> active_ctx_states) const {
  if (!old_root_ || old_offset >= old_root_->width || accepted_kinds.empty()) {
    return std::nullopt;
  }

  if (!cursor_.valid() || cursor_.root_node() != old_root_) {
    cursor_ = uast::Cursor(old_root_);
  }
  if (!cursor_.goto_byte(old_offset)) {
    return std::nullopt;
  }

  auto kind_matches = [&](uast::Kind k) noexcept {
    for (uast::Kind acc : accepted_kinds) {
      if (k == acc) return true;
    }
    return false;
  };

  // Walk upward to find the OUTERMOST rule node starting at old_offset matching accepted_kinds.
  // Checking only the outermost match prevents mistaking an inner left-recursive operand
  // for the outer left-recursive rule when the outer rule itself is dirty.
  const uast::GreenNode* outermost = nullptr;
  do {
    const auto r = cursor_.range();
    if (r.start != old_offset) {
      break;
    }
    const uast::GreenNode* gn = cursor_.green_node();
    if (gn && gn->is_rule() && kind_matches(gn->kind)) {
      outermost = gn;
    }
  } while (cursor_.parent());

  if (!outermost) {
    return std::nullopt;
  }
  if (!can_reuse(outermost, old_offset, expected_prec, active_ctx_states)) {
    return std::nullopt;
  }

  return RuleCandidate{
      outermost,
      old_offset,
      last_token_lex_state_out(outermost),
  };
}

bool ReuseOracle::can_splice_seq(const uast::GreenNode* seq,
                                 std::span<const uint32_t> active_ctx_states) const {
  if (!seq || !seq->is_seq()) {
    return false;
  }
  const auto* inner = seq->as_inner();
  if (inner->ctx_depth == UINT32_MAX) {
    return false;
  }
  if (inner->ctx_depth > 0) {
    if (inner->ctx_depth > active_ctx_states.size()) {
      return false;
    }
    if (compute_ctx_hash(active_ctx_states, inner->ctx_depth) != inner->ctx_hash) {
      return false;
    }
  }
  if (env_dirty_ && inner->pred_deps_hash != 0) {
    return false;
  }
  return true;
}

const uast::GreenNode* ReuseOracle::find_seq_starting_at(
    uint32_t seq_old_start,
    uast::Kind enclosing_rule_kind) const {
  if (!old_root_ || seq_old_start >= old_root_->width) {
    return nullptr;
  }

  const uast::GreenNode* cur = old_root_;
  uint32_t cur_off = 0;
  const uast::GreenNode* last_non_seq = nullptr;

  while (cur && cur->is_inner()) {
    if (cur->is_seq() && cur_off == seq_old_start) {
      if (enclosing_rule_kind == 0 ||
          (last_non_seq && last_non_seq->kind == enclosing_rule_kind)) {
        return cur;
      }
      return nullptr;
    }
    if (!cur->is_seq()) {
      last_non_seq = cur;
    }

    const auto* inner = cur->as_inner();
    const auto ch = inner->children();
    const uast::GreenNode* next_node = nullptr;
    uint32_t next_off = cur_off;
    uint32_t acc = cur_off;

    for (const auto& ref : ch) {
      const uast::GreenNode* c = ref.node;
      if (!c) continue;
      if (c->width == 0 && acc == seq_old_start && c->is_seq()) {
        next_node = c;
        next_off = acc;
        break;
      }
      if (seq_old_start >= acc && seq_old_start < acc + c->width) {
        next_node = c;
        next_off = acc;
        break;
      }
      acc += c->width;
    }
    cur = next_node;
    cur_off = next_off;
  }
  return nullptr;
}

uast::GreenPtr ReuseOracle::fast_forward_seq(
    const uast::GreenNode* seq,
    uint32_t seq_start_old,
    std::span<const uint32_t> active_ctx_states) const {
  if (!can_splice_seq(seq, active_ctx_states)) {
    return nullptr;
  }
  uast::GreenPtr prefix =
      uast::seq_longest_clean_prefix(seq, seq_start_old, dirty_spans_);
  if (!prefix) {
    return nullptr;
  }
  // Do not fast-forward items that have dirty-nearby errors.
  if (prefix->has_error() && is_near_dirty_error(prefix.get(), seq_start_old)) {
    return nullptr;
  }
  return prefix;
}

uast::GreenPtr ReuseOracle::adopt_suffix_seq(const uast::GreenNode* seq,
                                             uint32_t seq_start_old,
                                             uint32_t from_old_byte) const {
  if (!seq || !seq->is_seq()) {
    return nullptr;
  }
  uast::GreenPtr suffix =
      uast::seq_clean_run_from(seq, seq_start_old, from_old_byte, dirty_spans_);
  if (!suffix) {
    return nullptr;
  }
  if (suffix->has_error() && is_near_dirty_error(suffix.get(), from_old_byte)) {
    return nullptr;
  }
  return suffix;
}

} // namespace lx::parse
