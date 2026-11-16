#include "host/query_engine.hpp"

#include <functional>
#include <memory>
#include <regex>
#include <shared_mutex>
#include <unordered_map>

namespace lx::host {

namespace {

struct ChildItem {
  const lx::uast::GreenNode* node{nullptr};
  lx::uast::FieldId field{lx::uast::kNoField};
  uint32_t offset{0};
};

// Flattens transparent Seq nodes when inspecting logical children of a node,
// propagating inherited field_id from enclosing Seq wrappers.
void collect_logical_children(const lx::uast::GreenNode* node,
                              uint32_t base_offset,
                              lx::uast::FieldId inherited_field,
                              std::vector<ChildItem>& out) {
  if (!node || !node->is_inner()) return;
  const auto* inner = node->as_inner();
  uint32_t cur = base_offset;
  for (const auto& ref : inner->children()) {
    if (!ref.node) continue;
    lx::uast::FieldId eff =
        (ref.field_id != lx::uast::kNoField) ? ref.field_id : inherited_field;
    if (ref.node->is_seq()) {
      collect_logical_children(ref.node, cur, eff, out);
    } else {
      out.push_back(ChildItem{ref.node, eff, cur});
    }
    cur += ref.node->width;
  }
}

class RegexCache {
 public:
  static std::shared_ptr<const std::regex> get(const std::string& pattern) {
    static RegexCache instance;
    return instance.get_impl(pattern);
  }

 private:
  std::shared_ptr<const std::regex> get_impl(const std::string& pattern) {
    {
      std::shared_lock lock(mu_);
      auto it = cache_.find(pattern);
      if (it != cache_.end()) return it->second;
    }

    std::shared_ptr<const std::regex> compiled;
    try {
      compiled = std::make_shared<std::regex>(pattern);
    } catch (...) {
      compiled = nullptr;
    }

    std::unique_lock lock(mu_);
    if (cache_.size() >= 1024) {
      cache_.clear();
    }
    auto [it, _] = cache_.try_emplace(pattern, std::move(compiled));
    return it->second;
  }

  std::shared_mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<const std::regex>> cache_;
};

using SliceTextFn = std::function<std::string(uint32_t start, uint32_t end)>;

struct MatchContext {
  const LoadedModule* mod{nullptr};
  SliceTextFn slice_text;
};

bool node_matches_symbol(const lx::uast::GreenNode* node,
                         const QueryNode& qnode,
                         const MatchContext& ctx) {
  if (!node) return false;
  switch (qnode.kind) {
    case QueryNode::Kind::Wildcard:
      return !node->is_hidden();
    case QueryNode::Kind::Named: {
      if (!ctx.mod) return false;
      lx_symbol actual = lx::uast::kind_symbol(node->kind);
      auto it = ctx.mod->symbol_by_name.find(qnode.symbol_name);
      if (it == ctx.mod->symbol_by_name.end()) return false;
      return ctx.mod->is_subtype_of(actual, it->second);
    }
    case QueryNode::Kind::Anonymous: {
      return node->is_token() && !node->is_hidden();
    }
    default:
      return false;
  }
}

bool match_node(const lx::uast::GreenNode* node,
                uint32_t offset,
                lx::uast::FieldId field_id,
                const QueryNode& qnode,
                const MatchContext& ctx,
                const CompiledQuery& cq,
                std::vector<QueryCapture>& captures);

bool match_children_sequence(const std::vector<ChildItem>& items,
                             size_t item_idx,
                             const std::vector<QueryNode>& pattern_children,
                             size_t pat_idx,
                             const MatchContext& ctx,
                             const CompiledQuery& cq,
                             std::vector<QueryCapture>& captures) {
  while (pat_idx < pattern_children.size() &&
         !pattern_children[pat_idx].negated_field.empty()) {
    if (ctx.mod) {
      auto fit = ctx.mod->field_by_name.find(pattern_children[pat_idx].negated_field);
      if (fit != ctx.mod->field_by_name.end()) {
        for (const auto& ch : items) {
          if (ch.field == fit->second) return false;
        }
      }
    }
    ++pat_idx;
  }

  if (pat_idx == pattern_children.size()) {
    if (pat_idx > 0 && pattern_children[pat_idx - 1].anchor_after) {
      for (size_t i = item_idx; i < items.size(); ++i) {
        if (!items[i].node->is_hidden()) return false;
      }
    }
    return true;
  }

  const QueryNode& pe = pattern_children[pat_idx];
  const bool must_start_immediately = pe.anchor_before;

  auto skip_hidden = [&](size_t idx) -> size_t {
    while (idx < items.size() && items[idx].node->is_hidden()) ++idx;
    return idx;
  };

  // Attempts to match one instance of `pe` starting at `idx`, returning the
  // next item index after the match (or std::nullopt on failure).
  // Handles both single-item nodes and multi-element Group sequences.
  auto try_elem_at = [&](size_t idx, std::vector<QueryCapture>& local_caps)
      -> std::optional<size_t> {
    if (idx >= items.size()) return std::nullopt;
    if (pe.kind == QueryNode::Kind::Group && !pe.children.empty()) {
      // Match Group's children consecutively from `idx`
      // Ensure first element aligns at `idx` (anchored) if Group itself is anchored
      std::vector<QueryNode> sub_pats = pe.children;
      if (!sub_pats.empty()) {
        sub_pats[0].anchor_before = true;
      }
      // Find the first non-hidden item at/after idx for Group span
      size_t g_start = skip_hidden(idx);
      if (g_start >= items.size()) return std::nullopt;
      if (must_start_immediately && g_start != idx && !items[idx].node->is_hidden()) {
        return std::nullopt;
      }

      // Recursive continuation inside Group to know exact end item index
      std::function<std::optional<size_t>(size_t, size_t)> match_group =
          [&](size_t gi, size_t gp) -> std::optional<size_t> {
        if (gp == sub_pats.size()) return gi;
        const QueryNode& gpe = sub_pats[gp];
        size_t s = gpe.anchor_before ? skip_hidden(gi) : gi;
        size_t lim = gpe.anchor_before ? (s < items.size() ? s + 1 : s) : items.size();
        for (size_t k = s; k < lim; ++k) {
          if (items[k].node->is_hidden()) continue;
          std::vector<QueryCapture> saved = local_caps;
          if (match_node(items[k].node, items[k].offset, items[k].field,
                         gpe, ctx, cq, local_caps)) {
            if (auto next_i = match_group(k + 1, gp + 1)) {
              return next_i;
            }
          }
          local_caps = std::move(saved);
        }
        return std::nullopt;
      };

      auto end_idx = match_group(g_start, 0);
      if (!end_idx) return std::nullopt;
      if (!pe.capture_ids.empty() && *end_idx > g_start) {
        const uint32_t rs = items[g_start].offset;
        const auto& last_it = items[*end_idx - 1];
        const uint32_t re = last_it.offset + last_it.node->width;
        for (uint32_t cid : pe.capture_ids) {
          local_caps.push_back(QueryCapture{
              cid,
              cid < cq.capture_names.size() ? std::string_view(cq.capture_names[cid])
                                            : std::string_view{},
              items[g_start].node,
              lx::uast::Range{rs, re},
          });
        }
      }
      return end_idx;
    }

    const auto& ch = items[idx];
    if (ch.node->is_hidden()) return std::nullopt;
    if (match_node(ch.node, ch.offset, ch.field, pe, ctx, cq, local_caps)) {
      return idx + 1;
    }
    return std::nullopt;
  };

  if (pe.quantifier == QueryQuantifier::Optional) {
    size_t start_i = must_start_immediately ? skip_hidden(item_idx) : item_idx;
    size_t limit_i = must_start_immediately ? (start_i < items.size() ? start_i + 1 : start_i)
                                            : items.size();
    for (size_t i = start_i; i < limit_i; ++i) {
      std::vector<QueryCapture> saved = captures;
      if (auto next_i = try_elem_at(i, captures)) {
        if (match_children_sequence(items, *next_i, pattern_children, pat_idx + 1,
                                    ctx, cq, captures)) {
          return true;
        }
      }
      captures = std::move(saved);
    }
    return match_children_sequence(items, item_idx, pattern_children, pat_idx + 1,
                                   ctx, cq, captures);
  }

  if (pe.quantifier == QueryQuantifier::ZeroOrMore ||
      pe.quantifier == QueryQuantifier::OneOrMore) {
    size_t min_matches = (pe.quantifier == QueryQuantifier::OneOrMore) ? 1 : 0;
    std::function<bool(size_t, size_t)> match_rep = [&](size_t cur_i, size_t count) -> bool {
      size_t start_i = (count > 0 || must_start_immediately) ? skip_hidden(cur_i) : cur_i;
      if (start_i < items.size()) {
        if (count > 0 || must_start_immediately) {
          std::vector<QueryCapture> saved = captures;
          if (auto next_i = try_elem_at(start_i, captures)) {
            if (*next_i > start_i && match_rep(*next_i, count + 1)) return true;
          }
          captures = std::move(saved);
        } else {
          for (size_t i = start_i; i < items.size(); ++i) {
            std::vector<QueryCapture> saved = captures;
            if (auto next_i = try_elem_at(i, captures)) {
              if (*next_i > i && match_rep(*next_i, count + 1)) return true;
            }
            captures = std::move(saved);
          }
        }
      }
      if (count >= min_matches) {
        return match_children_sequence(items, cur_i, pattern_children, pat_idx + 1,
                                       ctx, cq, captures);
      }
      return false;
    };
    return match_rep(item_idx, 0);
  }

  // QueryQuantifier::One
  size_t start_i = must_start_immediately ? skip_hidden(item_idx) : item_idx;
  size_t limit_i = must_start_immediately ? (start_i < items.size() ? start_i + 1 : start_i)
                                          : items.size();
  for (size_t i = start_i; i < limit_i; ++i) {
    std::vector<QueryCapture> saved = captures;
    if (auto next_i = try_elem_at(i, captures)) {
      if (match_children_sequence(items, *next_i, pattern_children, pat_idx + 1,
                                  ctx, cq, captures)) {
        return true;
      }
    }
    captures = std::move(saved);
  }
  return false;
}

bool match_node(const lx::uast::GreenNode* node,
                uint32_t offset,
                lx::uast::FieldId field_id,
                const QueryNode& qnode,
                const MatchContext& ctx,
                const CompiledQuery& cq,
                std::vector<QueryCapture>& captures) {
  if (!node) return false;

  if (!qnode.field_name.empty()) {
    if (!ctx.mod) return false;
    auto fit = ctx.mod->field_by_name.find(qnode.field_name);
    if (fit == ctx.mod->field_by_name.end() || field_id != fit->second) {
      return false;
    }
  }

  auto push_captures = [&]() {
    for (uint32_t cid : qnode.capture_ids) {
      captures.push_back(QueryCapture{
          cid,
          cid < cq.capture_names.size() ? std::string_view(cq.capture_names[cid])
                                        : std::string_view{},
          node,
          lx::uast::Range{offset, offset + node->width},
      });
    }
  };

  if (qnode.kind == QueryNode::Kind::Alternation) {
    for (const auto& alt : qnode.children) {
      std::vector<QueryCapture> saved = captures;
      if (match_node(node, offset, field_id, alt, ctx, cq, captures)) {
        push_captures();
        return true;
      }
      captures = std::move(saved);
    }
    return false;
  }

  if (qnode.kind == QueryNode::Kind::Group) {
    if (qnode.children.size() == 1) {
      std::vector<QueryCapture> saved = captures;
      if (match_node(node, offset, field_id, qnode.children[0], ctx, cq, captures)) {
        push_captures();
        return true;
      }
      captures = std::move(saved);
      return false;
    }
    if (!qnode.children.empty() && node->is_inner()) {
      std::vector<ChildItem> items;
      collect_logical_children(node, offset, lx::uast::kNoField, items);
      if (match_children_sequence(items, 0, qnode.children, 0, ctx, cq, captures)) {
        push_captures();
        return true;
      }
    }
    return false;
  }

  if (qnode.kind == QueryNode::Kind::Anonymous) {
    if (!node->is_token() || node->is_hidden()) return false;
    std::string txt = ctx.slice_text(offset, offset + node->width);
    if (txt != qnode.symbol_name) return false;
    push_captures();
    return true;
  }

  if (!node_matches_symbol(node, qnode, ctx)) {
    return false;
  }

  if (!qnode.children.empty()) {
    if (!node->is_inner()) return false;
    std::vector<ChildItem> items;
    collect_logical_children(node, offset, lx::uast::kNoField, items);
    if (!match_children_sequence(items, 0, qnode.children, 0, ctx, cq, captures)) {
      return false;
    }
  }

  push_captures();
  return true;
}

bool eval_predicates(const QueryPattern& pat,
                     const std::vector<QueryCapture>& captures,
                     const MatchContext& ctx) {
  auto resolve_arg = [&](const QueryPredicateArg& arg) -> std::optional<std::string> {
    if (!arg.is_capture) return arg.value;
    for (const auto& c : captures) {
      if (c.capture_id == arg.capture_idx) {
        return ctx.slice_text(c.range.start, c.range.end);
      }
    }
    return std::nullopt;
  };

  for (const auto& pred : pat.predicates) {
    if (pred.args.size() < 2) continue;
    auto lhs = resolve_arg(pred.args[0]);
    if (!lhs) return false;

    switch (pred.op) {
      case QueryPredOp::Eq:
      case QueryPredOp::NotEq: {
        auto rhs = resolve_arg(pred.args[1]);
        if (!rhs) return false;
        const bool eq = (*lhs == *rhs);
        if (pred.op == QueryPredOp::Eq && !eq) return false;
        if (pred.op == QueryPredOp::NotEq && eq) return false;
        break;
      }
      case QueryPredOp::Match:
      case QueryPredOp::NotMatch: {
        auto rhs = resolve_arg(pred.args[1]);
        if (!rhs) return false;
        bool matched = false;
        if (auto re = RegexCache::get(*rhs)) {
          matched = std::regex_search(*lhs, *re);
        }
        if (pred.op == QueryPredOp::Match && !matched) return false;
        if (pred.op == QueryPredOp::NotMatch && matched) return false;
        break;
      }
      case QueryPredOp::AnyOf: {
        bool found = false;
        for (size_t i = 1; i < pred.args.size(); ++i) {
          auto rhs = resolve_arg(pred.args[i]);
          if (rhs && *lhs == *rhs) {
            found = true;
            break;
          }
        }
        if (!found) return false;
        break;
      }
    }
  }
  return true;
}

std::vector<QueryMatch> execute_impl(const CompiledQuery& query,
                                     const lx::uast::GreenNode* root,
                                     const MatchContext& ctx,
                                     std::optional<lx::uast::Range> filter_range,
                                     uint32_t root_offset) {
  std::vector<QueryMatch> matches;
  if (!root || query.patterns.empty()) return matches;

  std::function<void(const lx::uast::GreenNode*, uint32_t, lx::uast::FieldId)> visit =
      [&](const lx::uast::GreenNode* node, uint32_t offset, lx::uast::FieldId field_id) {
        if (!node) return;
        const uint32_t end = offset + node->width;
        if (filter_range.has_value()) {
          if (end <= filter_range->start || offset >= filter_range->end) {
            return;
          }
        }

        if (!node->is_seq()) {
          for (const auto& pat : query.patterns) {
            std::vector<QueryCapture> caps;
            if (match_node(node, offset, field_id, pat.root, ctx, query, caps)) {
              if (eval_predicates(pat, caps, ctx)) {
                matches.push_back(QueryMatch{pat.pattern_index, std::move(caps)});
              }
            }
          }
        }

        if (node->is_inner()) {
          const auto* inner = node->as_inner();
          uint32_t cur = offset;
          for (const auto& ref : inner->children()) {
            if (ref.node) {
              lx::uast::FieldId child_fid =
                  (ref.field_id != lx::uast::kNoField) ? ref.field_id
                  : (node->is_seq() ? field_id : lx::uast::kNoField);
              visit(ref.node, cur, child_fid);
              cur += ref.node->width;
            }
          }
        }
      };

  visit(root, root_offset, lx::uast::kNoField);
  return matches;
}

}  // namespace

std::vector<QueryMatch> QueryEngine::execute(
    const CompiledQuery& query,
    const lx::uast::GreenNode* root,
    const lx::Rope& rope,
    const LoadedModule* mod,
    std::optional<lx::uast::Range> filter_range,
    uint32_t root_offset) {
  MatchContext ctx;
  ctx.mod = mod;
  ctx.slice_text = [&rope](uint32_t s, uint32_t e) {
    return rope.slice_text(s, e);
  };
  return execute_impl(query, root, ctx, filter_range, root_offset);
}

std::vector<QueryMatch> QueryEngine::execute(
    const CompiledQuery& query,
    const lx::uast::GreenNode* root,
    const lx::RopeView& view,
    const LoadedModule* mod,
    std::optional<lx::uast::Range> filter_range) {
  MatchContext ctx;
  ctx.mod = mod;
  ctx.slice_text = [&view](uint32_t s, uint32_t e) {
    return view.slice_text(s, e);
  };
  return execute_impl(query, root, ctx, filter_range, 0);
}

}  // namespace lx::host
