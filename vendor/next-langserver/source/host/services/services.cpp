#include "host/services.hpp"
#include "host/services/c_family_outline.hpp"
#include "host/services/query_cache.hpp"

#include "host/extensions.hpp"
#include "lx/lx_constants.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_set>

namespace lx::host {

namespace {

std::string to_lower(std::string_view sv) {
  std::string out(sv);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::optional<SemanticTokenType> token_type_from_capture(std::string_view cap) {
  auto dot = cap.find('.');
  std::string_view base = (dot == std::string_view::npos) ? cap : cap.substr(0, dot);
  if (base == "namespace" || base == "module") return SemanticTokenType::Namespace;
  if (base == "type" || base == "type_builtin") return SemanticTokenType::Type;
  if (base == "class") return SemanticTokenType::Class;
  if (base == "enum") return SemanticTokenType::Enum;
  if (base == "interface") return SemanticTokenType::Interface;
  if (base == "struct") return SemanticTokenType::Struct;
  if (base == "typeParameter") return SemanticTokenType::TypeParameter;
  if (base == "parameter") return SemanticTokenType::Parameter;
  if (base == "variable") return SemanticTokenType::Variable;
  if (base == "property" || base == "field" || base == "attribute") return SemanticTokenType::Property;
  if (base == "enumMember" || base == "constant") return SemanticTokenType::EnumMember;
  if (base == "function") return SemanticTokenType::Function;
  if (base == "method") return SemanticTokenType::Method;
  if (base == "macro") return SemanticTokenType::Macro;
  if (base == "keyword" || base == "boolean") return SemanticTokenType::Keyword;
  if (base == "modifier") return SemanticTokenType::Modifier;
  if (base == "comment") return SemanticTokenType::Comment;
  if (base == "string" || base == "escape") return SemanticTokenType::String;
  if (base == "number" || base == "float" || base == "integer") return SemanticTokenType::Number;
  if (base == "regexp") return SemanticTokenType::Regexp;
  if (base == "operator" || base == "punctuation") return SemanticTokenType::Operator;
  return std::nullopt;
}

uint32_t modifiers_from_capture(std::string_view cap) {
  uint32_t mods = 0;
  if (cap.find(".declaration") != std::string_view::npos) mods |= (1u << 0);
  if (cap.find(".definition") != std::string_view::npos) mods |= (1u << 1);
  if (cap.find(".readonly") != std::string_view::npos) mods |= (1u << 2);
  if (cap.find(".static") != std::string_view::npos) mods |= (1u << 3);
  if (cap.find(".deprecated") != std::string_view::npos) mods |= (1u << 4);
  return mods;
}

std::optional<SemanticTokenType> classify_symbol_heuristic(std::string_view sym_name,
                                                           uint32_t sym_flags,
                                                           std::string_view text) {
  if (sym_flags & LX_SYM_HIDDEN) {
    std::string lower = to_lower(sym_name);
    if (lower.find("comment") != std::string::npos || lower.find("doc") != std::string::npos) {
      return SemanticTokenType::Comment;
    }
    return std::nullopt;
  }

  std::string lower = to_lower(sym_name);
  if (lower.find("comment") != std::string::npos) return SemanticTokenType::Comment;
  if (lower.find("ident") != std::string::npos || lower == "id" || lower == "name" || lower == "varid") {
    return SemanticTokenType::Variable;
  }
  if (lower == "conid") return SemanticTokenType::Type;
  // Generated lexers keep names such as FUN, FUNC, INT, MODULE and RETURN.
  // Their spelling identifies keywords before substring tests (INT is not a number).
  if (!text.empty() && lower == to_lower(text) &&
      std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isalpha(c) || c == '_'; })) {
    return SemanticTokenType::Keyword;
  }

  if (lower.find("string") != std::string::npos || lower.find("str") != std::string::npos ||
      lower.find("char") != std::string::npos) {
    return SemanticTokenType::String;
  }
  if (lower.find("num") != std::string::npos || lower.find("int") != std::string::npos ||
      lower.find("float") != std::string::npos || lower.find("digit") != std::string::npos) {
    return SemanticTokenType::Number;
  }
  if (lower.find("regex") != std::string::npos) return SemanticTokenType::Regexp;
  if (lower == "true" || lower == "false" || lower == "null" ||
      lower.find("kw") != std::string::npos || lower.find("keyword") != std::string::npos ||
      lower.find("bool") != std::string::npos) {
    return SemanticTokenType::Keyword;
  }
  if (lower.find("ident") != std::string::npos || lower == "id" || lower == "name") {
    return SemanticTokenType::Variable;
  }
  if (lower.find("type") != std::string::npos) {
    return SemanticTokenType::Type;
  }
  if (lower.find("op") != std::string::npos || lower == "plus" || lower == "minus" ||
      lower == "star" || lower == "slash" || lower == "eq" || lower == "assign") {
    return SemanticTokenType::Operator;
  }

  if (!sym_name.empty() && sym_name.front() == '\'' && sym_name.back() == '\'' && sym_name.size() >= 3) {
    bool has_alpha = false;
    for (size_t i = 1; i + 1 < sym_name.size(); ++i) {
      if (std::isalpha(static_cast<unsigned char>(sym_name[i]))) {
        has_alpha = true;
        break;
      }
    }
    if (has_alpha) return SemanticTokenType::Keyword;
  }

  if (!text.empty()) {
    bool all_alpha = true;
    for (char c : text) {
      if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') {
        all_alpha = false;
        break;
      }
    }
    if (all_alpha && (text == "true" || text == "false" || text == "null" ||
                      text == "if" || text == "else" || text == "for" ||
                      text == "while" || text == "return" || text == "fn" ||
                      text == "def" || text == "class" || text == "import")) {
      return SemanticTokenType::Keyword;
    }
  }
  return std::nullopt;
}

std::string get_highlight_query(const LoadedModule* mod) {
  if (!mod) return {};
  if (const void* raw = mod->safe_get_extension("lx.highlight.v1")) {
    const auto* ext = static_cast<const lx_ext_highlight_v1*>(raw);
    if (ext->size >= sizeof(lx_ext_highlight_v1) && ext->query_scm.ptr && ext->query_scm.len > 0) {
      return std::string(ext->query_scm.ptr, ext->query_scm.len);
    }
  }
  return mod->manifest.highlight_query;
}

std::string get_fold_query(const LoadedModule* mod) {
  if (!mod) return {};
  if (const void* raw = mod->safe_get_extension("lx.fold.v1")) {
    const auto* ext = static_cast<const lx_ext_fold_v1*>(raw);
    if (ext->size >= sizeof(lx_ext_fold_v1) && ext->query_scm.ptr && ext->query_scm.len > 0) {
      return std::string(ext->query_scm.ptr, ext->query_scm.len);
    }
  }
  return mod->manifest.fold_query;
}

std::string get_outline_query(const LoadedModule* mod) {
  if (!mod) return {};
  if (const void* raw = mod->safe_get_extension("lx.outline.v1")) {
    const auto* ext = static_cast<const lx_ext_outline_v1*>(raw);
    if (ext->size >= sizeof(lx_ext_outline_v1) && ext->query_scm.ptr && ext->query_scm.len > 0) {
      return std::string(ext->query_scm.ptr, ext->query_scm.len);
    }
  }
  return mod->manifest.outline_query;
}

void sort_and_dedup_spans(std::vector<SemanticTokenSpan>& out) {
  std::sort(out.begin(), out.end(), [](const SemanticTokenSpan& a, const SemanticTokenSpan& b) {
    if (a.offset != b.offset) return a.offset < b.offset;
    return a.length < b.length;
  });

  std::vector<SemanticTokenSpan> dedup;
  dedup.reserve(out.size());
  uint32_t last_end = 0;
  for (const auto& s : out) {
    if (s.length == 0) continue;
    if (s.offset < last_end) continue;
    dedup.push_back(s);
    last_end = s.offset + s.length;
  }
  out = std::move(dedup);
}

template <typename SliceFn>
std::vector<SemanticTokenSpan> highlight_impl(
    const lx::uast::GreenNode* root,
    const LoadedModule* mod,
    SliceFn&& slice_fn,
    std::optional<lx::uast::Range> visible_range,
    const lx::Rope* rope,
    const lx::RopeView* view) {
  std::vector<SemanticTokenSpan> out;
  if (!root) return out;

  std::string qsrc = get_highlight_query(mod);
  if (!qsrc.empty()) {
    if (auto cached = services::QueryCache::global().get_or_compile(qsrc)) {
      std::vector<QueryMatch> matches;
      if (rope) {
        matches = QueryEngine::execute(*cached->query, root, *rope, mod, visible_range);
      } else if (view) {
        matches = QueryEngine::execute(*cached->query, root, *view, mod, visible_range);
      }
      for (const auto& m : matches) {
        for (const auto& c : m.captures) {
          if (c.range.empty()) continue;
          auto tt = token_type_from_capture(c.capture_name);
          if (!tt) continue;
          out.push_back(SemanticTokenSpan{
              c.range.start,
              c.range.length(),
              static_cast<uint32_t>(*tt),
              modifiers_from_capture(c.capture_name),
          });
        }
      }
    }
  }

  if (out.empty()) {
    std::function<void(const lx::uast::GreenNode*, uint32_t)> walk =
        [&](const lx::uast::GreenNode* node, uint32_t off) {
          if (!node) return;
          const uint32_t end = off + node->width;
          if (visible_range.has_value()) {
            if (end <= visible_range->start || off >= visible_range->end) return;
          }
          if (node->is_token()) {
            if (node->width == 0 || node->is_error() || node->is_missing()) return;
            lx_symbol sym = lx::uast::kind_symbol(node->kind);
            std::string_view sname = mod ? mod->symbol_name(sym) : std::string_view{};
            uint32_t sflags = mod ? mod->symbol_flags(sym) : (node->is_hidden() ? LX_SYM_HIDDEN : 0u);
            std::string txt = slice_fn(off, end);
            auto tt = classify_symbol_heuristic(sname, sflags, txt);
            if (tt) {
              out.push_back(SemanticTokenSpan{
                  off, node->width, static_cast<uint32_t>(*tt), 0,
              });
            }
            return;
          }
          if (node->is_inner()) {
            uint32_t cur = off;
            for (const auto& ref : node->as_inner()->children()) {
              if (ref.node) {
                walk(ref.node, cur);
                cur += ref.node->width;
              }
            }
          }
        };
    walk(root, 0);
  }

  sort_and_dedup_spans(out);
  return out;
}

int guess_symbol_kind(std::string_view name) {
  std::string lower = to_lower(name);
  if (lower.find("namespace") != std::string::npos || lower.find("module") != std::string::npos) return 3;
  if (lower.find("class") != std::string::npos) return 5;
  if (lower.find("method") != std::string::npos) return 6;
  if (lower.find("prop") != std::string::npos || lower.find("pair") != std::string::npos ||
      lower.find("member") != std::string::npos) return 7;
  if (lower.find("field") != std::string::npos) return 8;
  if (lower.find("enum") != std::string::npos) return 10;
  if (lower.find("iface") != std::string::npos || lower.find("interface") != std::string::npos) return 11;
  if (lower.find("func") != std::string::npos || lower == "fn" || lower.find("rule") != std::string::npos) return 12;
  if (lower.find("struct") != std::string::npos) return 23;
  if (lower.find("obj") != std::string::npos) return 19;
  if (lower.find("arr") != std::string::npos) return 18;
  return 13;  // Variable
}

// ============================================================================
// ATN Cursor-Position Expected-Tokens Helpers (§11)
// ============================================================================

struct RuleLookupResult {
  uint16_t rule_id{lx::atn::kIndexNoneU16};
  uint32_t entry_state{lx::atn::kIndexNoneU32};
};

RuleLookupResult find_atn_rule(const lx::atn::Atn& atn, lx_symbol sym) noexcept {
  const auto rules = atn.rules();
  for (uint16_t i = 0; i < rules.size(); ++i) {
    if (rules[i].symbol_id == sym) {
      return {i, rules[i].start_state};
    }
  }
  for (const auto& alt : atn.alt_labels()) {
    if (alt.symbol_id == sym && alt.rule_id < rules.size()) {
      return {alt.rule_id, alt.entry_state};
    }
  }
  return {};
}

std::vector<uint32_t> epsilon_closure(const lx::atn::Atn& atn,
                                      std::span<const uint32_t> seeds) {
  std::vector<uint32_t> out;
  std::vector<bool> seen(atn.states().size(), false);
  std::vector<uint32_t> work(seeds.begin(), seeds.end());

  while (!work.empty()) {
    uint32_t st = work.back();
    work.pop_back();
    if (st >= seen.size() || seen[st]) continue;
    seen[st] = true;
    out.push_back(st);

    for (const auto& tr : atn.state_transitions(st)) {
      using TK = lx::atn::TransitionKind;
      if (tr.kind == TK::Epsilon || tr.kind == TK::Pred ||
          tr.kind == TK::Precedence || tr.kind == TK::Action) {
        work.push_back(tr.to);
      }
    }
  }
  return out;
}

std::vector<uint32_t> step_atn_terminal(const lx::atn::Atn& atn,
                                        std::span<const uint32_t> states,
                                        lx_symbol term) {
  std::vector<uint32_t> next_seeds;
  for (uint32_t st : states) {
    for (const auto& tr : atn.state_transitions(st)) {
      using TK = lx::atn::TransitionKind;
      if (tr.kind == TK::Atom || tr.kind == TK::Set || tr.kind == TK::NotSet ||
          tr.kind == TK::Range || tr.kind == TK::Wildcard) {
        if (atn.transition_matches(tr, term)) {
          next_seeds.push_back(tr.to);
        }
      }
    }
  }
  return epsilon_closure(atn, next_seeds);
}

std::vector<uint32_t> step_atn_rule(const lx::atn::Atn& atn,
                                    std::span<const uint32_t> states,
                                    uint16_t callee_rule_id) {
  std::vector<uint32_t> next_seeds;
  for (uint32_t st : states) {
    for (const auto& tr : atn.state_transitions(st)) {
      if (tr.kind == lx::atn::TransitionKind::Rule && tr.arg0 == callee_rule_id) {
        next_seeds.push_back(tr.to);
      }
    }
  }
  return epsilon_closure(atn, next_seeds);
}

struct LogicalChild {
  const lx::uast::GreenNode* node{nullptr};
  uint32_t start{0};
  uint32_t end{0};
};

void collect_rule_children(const lx::uast::GreenNode* node,
                           uint32_t offset,
                           std::vector<LogicalChild>& out) {
  if (!node || !node->is_inner()) return;
  uint32_t cur = offset;
  for (const auto& ref : node->as_inner()->children()) {
    if (!ref.node) continue;
    if (ref.node->is_seq()) {
      collect_rule_children(ref.node, cur, out);
    } else {
      out.push_back(LogicalChild{ref.node, cur, cur + ref.node->width});
    }
    cur += ref.node->width;
  }
}

struct ActiveAtnFrame {
  uint16_t rule_id{0};
  std::vector<uint32_t> active_states;
};

void build_active_atn_stack(const lx::atn::Atn& atn,
                            const lx::uast::GreenNode* node,
                            uint32_t offset,
                            uint32_t byte_offset,
                            std::vector<ActiveAtnFrame>& stack) {
  if (!node || !node->is_inner() || node->is_seq()) return;
  auto rl = find_atn_rule(atn, lx::uast::kind_symbol(node->kind));
  if (rl.rule_id == lx::atn::kIndexNoneU16) return;

  std::vector<uint32_t> states = epsilon_closure(atn, std::span<const uint32_t>(&rl.entry_state, 1));

  std::vector<LogicalChild> children;
  collect_rule_children(node, offset, children);

  const LogicalChild* active_subrule = nullptr;
  for (const auto& ch : children) {
    if (ch.node->is_hidden() || ch.node->is_missing() || ch.node->is_error()) {
      continue;
    }

    // Consume children strictly before byte_offset (or tokens ending at byte_offset).
    if (ch.end < byte_offset || (ch.end == byte_offset && ch.node->is_token())) {
      if (ch.node->is_token()) {
        auto next = step_atn_terminal(atn, states, lx::uast::kind_symbol(ch.node->kind));
        if (!next.empty()) states = std::move(next);
      } else if (ch.node->is_rule()) {
        auto crl = find_atn_rule(atn, lx::uast::kind_symbol(ch.node->kind));
        if (crl.rule_id != lx::atn::kIndexNoneU16) {
          auto next = step_atn_rule(atn, states, crl.rule_id);
          if (!next.empty()) states = std::move(next);
        }
      }
      continue;
    }

    // Descend into non-empty rule spanning across or ending right at byte_offset.
    if (ch.start < byte_offset && ch.node->is_inner() && ch.node->is_rule()) {
      active_subrule = &ch;
    }
    break;
  }

  stack.push_back(ActiveAtnFrame{rl.rule_id, std::move(states)});
  if (active_subrule) {
    build_active_atn_stack(atn, active_subrule->node, active_subrule->start, byte_offset, stack);
  }
}

void collect_first_from_stack(const lx::atn::Atn& atn,
                              uint32_t symbol_count,
                              const std::vector<ActiveAtnFrame>& stack,
                              const std::function<void(lx_symbol)>& add_sym) {
  if (stack.empty()) return;

  // Worklist items carry (state_id, stack_depth_idx) so RuleStop propagates cleanly
  // to the caller's follow state without entering arbitrary unrelated callers.
  struct WorkItem {
    uint32_t state;
    int stack_idx; // >= 0: active stack frame index; -1: free callee expansion
  };

  const size_t num_states = atn.states().size();
  std::vector<bool> visited_free(num_states, false);
  std::vector<std::vector<bool>> visited_stack(stack.size(),
                                               std::vector<bool>(num_states, false));

  std::vector<WorkItem> work;
  const int top_idx = static_cast<int>(stack.size()) - 1;
  for (uint32_t st : stack.back().active_states) {
    work.push_back(WorkItem{st, top_idx});
  }

  while (!work.empty()) {
    const WorkItem wi = work.back();
    work.pop_back();
    if (wi.state >= num_states) continue;

    if (wi.stack_idx >= 0) {
      if (visited_stack[static_cast<size_t>(wi.stack_idx)][wi.state]) continue;
      visited_stack[static_cast<size_t>(wi.stack_idx)][wi.state] = true;
    } else {
      if (visited_free[wi.state]) continue;
      visited_free[wi.state] = true;
    }

    const auto& st_obj = atn.state(wi.state);
    if (st_obj.kind == lx::atn::StateKind::RuleStop && wi.stack_idx > 0) {
      const uint16_t finished_rule = stack[static_cast<size_t>(wi.stack_idx)].rule_id;
      const int parent_idx = wi.stack_idx - 1;
      for (uint32_t pst : stack[static_cast<size_t>(parent_idx)].active_states) {
        for (const auto& ptr : atn.state_transitions(pst)) {
          if (ptr.kind == lx::atn::TransitionKind::Rule && ptr.arg0 == finished_rule) {
            work.push_back(WorkItem{ptr.to, parent_idx});
          }
        }
      }
      continue;
    }

    for (const auto& tr : atn.state_transitions(wi.state)) {
      using TK = lx::atn::TransitionKind;
      switch (tr.kind) {
        case TK::Epsilon:
        case TK::Pred:
        case TK::Precedence:
        case TK::Action:
          work.push_back(WorkItem{tr.to, wi.stack_idx});
          break;
        case TK::Rule:
          if (tr.arg0 < atn.rules().size()) {
            work.push_back(WorkItem{atn.rule(static_cast<uint16_t>(tr.arg0)).start_state, -1});
          }
          break;
        case TK::Atom:
          add_sym(static_cast<lx_symbol>(tr.arg0));
          break;
        case TK::Range:
          for (uint32_t s = tr.arg0; s <= tr.arg1 && s < symbol_count; ++s) {
            add_sym(static_cast<lx_symbol>(s));
          }
          break;
        case TK::Set:
          if (tr.arg0 < atn.sets().size()) {
            for (const auto& iv : atn.set_intervals(static_cast<uint16_t>(tr.arg0))) {
              for (uint32_t s = iv.lo; s <= iv.hi && s < symbol_count; ++s) {
                add_sym(static_cast<lx_symbol>(s));
              }
            }
          }
          break;
        case TK::NotSet:
        case TK::Wildcard:
          for (uint32_t s = LX_FIRST_USER_SYMBOL; s < symbol_count; ++s) {
            if (tr.kind == TK::Wildcard ||
                (tr.arg0 < atn.sets().size() &&
                 !atn.set_contains(static_cast<uint16_t>(tr.arg0), static_cast<lx_symbol>(s)))) {
              add_sym(static_cast<lx_symbol>(s));
            }
          }
          break;
      }
    }
  }
}

// Finds paired open/close symbols in an ATN rule when a rule begins with a keyword/delimiter
// and closes with a distinct keyword/delimiter (e.g. BEGIN/END, IF/FI).
std::optional<std::pair<lx_symbol, bool>> find_atn_paired_symbol(
    const lx::atn::Atn& atn,
    lx_symbol parent_rule_sym,
    lx_symbol tok_sym) noexcept {
  auto rl = find_atn_rule(atn, parent_rule_sym);
  if (rl.rule_id == lx::atn::kIndexNoneU16) return std::nullopt;
  const auto& rule = atn.rule(rl.rule_id);

  // Find direct single-atom opening/closing terminals adjacent to RuleStart / RuleStop.
  std::vector<lx_symbol> open_syms;
  std::vector<lx_symbol> close_syms;

  std::vector<uint32_t> start_cl = epsilon_closure(atn, std::span<const uint32_t>(&rule.start_state, 1));
  for (uint32_t st : start_cl) {
    for (const auto& tr : atn.state_transitions(st)) {
      if (tr.kind == lx::atn::TransitionKind::Atom) {
        open_syms.push_back(static_cast<lx_symbol>(tr.arg0));
      }
    }
  }

  // States that can reach rule.stop_state via epsilon after one Atom transition
  for (const auto& tr : atn.transitions()) {
    if (tr.kind == lx::atn::TransitionKind::Atom) {
      if (tr.to < atn.states().size() && atn.state(tr.to).rule_id == rl.rule_id) {
        auto cl = epsilon_closure(atn, std::span<const uint32_t>(&tr.to, 1));
        for (uint32_t s : cl) {
          if (s == rule.stop_state) {
            close_syms.push_back(static_cast<lx_symbol>(tr.arg0));
            break;
          }
        }
      }
    }
  }

  if (open_syms.size() == 1 && close_syms.size() == 1 && open_syms[0] != close_syms[0]) {
    if (tok_sym == open_syms[0]) return std::make_pair(close_syms[0], true);
    if (tok_sym == close_syms[0]) return std::make_pair(open_syms[0], false);
  }
  return std::nullopt;
}

}  // namespace

// ============================================================================
// Services Implementation
// ============================================================================

std::vector<SemanticTokenSpan> Services::highlight(
    const Snapshot& snapshot,
    std::optional<lx::uast::Range> visible_range) {
  return highlight_impl(
      snapshot.root.get(),
      snapshot.module.get(),
      [&](uint32_t s, uint32_t e) { return snapshot.rope.slice_text(s, e); },
      visible_range,
      &snapshot.rope,
      nullptr);
}

std::vector<SemanticTokenSpan> Services::highlight_view(
    const lx::uast::GreenNode* root,
    const lx::RopeView& view,
    const LoadedModule* mod,
    std::optional<lx::uast::Range> visible_range) {
  return highlight_impl(
      root,
      mod,
      [&](uint32_t s, uint32_t e) { return view.slice_text(s, e); },
      visible_range,
      nullptr,
      &view);
}

std::vector<uint32_t> Services::encode_lsp_semantic_tokens(
    const lx::Rope& rope,
    const std::vector<SemanticTokenSpan>& spans) {
  std::vector<uint32_t> data;
  data.reserve(spans.size() * 5);

  uint32_t prev_line = 0;
  uint32_t prev_char = 0;

  auto utf16_col = [&](uint32_t line, uint32_t byte_off) -> uint32_t {
    uint32_t ls = rope.line_start_byte(line);
    return rope.byte_to_utf16(byte_off) - rope.byte_to_utf16(ls);
  };

  for (const auto& s : spans) {
    if (s.length == 0 || s.offset >= rope.byte_length()) continue;
    uint32_t end_off = std::min(s.offset + s.length, rope.byte_length());
    lx::Point p_start = rope.byte_to_point(s.offset);
    lx::Point p_end = rope.byte_to_point(end_off);

    for (uint32_t line = p_start.line; line <= p_end.line; ++line) {
      uint32_t seg_start_b = (line == p_start.line) ? s.offset : rope.line_start_byte(line);
      uint32_t seg_end_b = (line == p_end.line) ? end_off : rope.line_end_byte(line);
      if (seg_end_b <= seg_start_b) continue;

      uint32_t start_u16 = utf16_col(line, seg_start_b);
      uint32_t end_u16 = utf16_col(line, seg_end_b);
      if (end_u16 <= start_u16) continue;
      uint32_t len_u16 = end_u16 - start_u16;

      uint32_t delta_line = line - prev_line;
      uint32_t delta_char = (delta_line == 0) ? (start_u16 - prev_char) : start_u16;

      data.push_back(delta_line);
      data.push_back(delta_char);
      data.push_back(len_u16);
      data.push_back(s.token_type);
      data.push_back(s.modifiers);

      prev_line = line;
      prev_char = start_u16;
    }
  }
  return data;
}

std::vector<FoldRange> Services::fold(const Snapshot& snapshot) {
  std::vector<FoldRange> out;
  if (!snapshot.root) return out;
  const LoadedModule* mod = snapshot.module.get();

  std::string qsrc = get_fold_query(mod);
  if (!qsrc.empty()) {
    if (auto cached = services::QueryCache::global().get_or_compile(qsrc)) {
      auto matches = QueryEngine::execute(*cached->query, snapshot.root.get(), snapshot.rope, mod);
      for (const auto& m : matches) {
        for (const auto& c : m.captures) {
          if (c.range.empty()) continue;
          auto p0 = snapshot.rope.byte_to_point(c.range.start);
          auto p1 = snapshot.rope.byte_to_point(c.range.end > 0 ? c.range.end - 1 : 0);
          if (p1.line > p0.line) {
            std::string kind = "region";
            if (c.capture_name.find("comment") != std::string_view::npos) {
              kind = "comment";
            } else if (c.capture_name.find("import") != std::string_view::npos) {
              kind = "imports";
            }
            out.push_back(FoldRange{p0.line, p1.line, std::move(kind)});
          }
        }
      }
    }
  }

  if (out.empty()) {
    std::function<void(const lx::uast::GreenNode*, uint32_t, bool)> walk =
        [&](const lx::uast::GreenNode* node, uint32_t off, bool is_root) {
          if (!node) return;
          if (node->width > 0) {
            auto p0 = snapshot.rope.byte_to_point(off);
            auto p1 = snapshot.rope.byte_to_point(off + node->width - 1);
            if (p1.line <= p0.line) return;

            if (node->is_token() && node->is_hidden()) {
              out.push_back(FoldRange{p0.line, p1.line, "comment"});
              return;
            }
            if (!is_root && node->is_rule() && !node->is_seq() && !node->is_error()) {
              out.push_back(FoldRange{p0.line, p1.line, "region"});
            }
          }
          if (node->is_inner()) {
            uint32_t cur = off;
            for (const auto& ref : node->as_inner()->children()) {
              if (ref.node) {
                walk(ref.node, cur, false);
                cur += ref.node->width;
              }
            }
          }
        };
    walk(snapshot.root.get(), 0, true);
  }

  std::sort(out.begin(), out.end(), [](const FoldRange& a, const FoldRange& b) {
    if (a.start_line != b.start_line) return a.start_line < b.start_line;
    return a.end_line > b.end_line;
  });
  std::vector<FoldRange> unique;
  unique.reserve(out.size());
  for (const auto& f : out) {
    if (!unique.empty() && unique.back().start_line == f.start_line &&
        unique.back().end_line == f.end_line) {
      continue;
    }
    unique.push_back(f);
  }
  return unique;
}

std::vector<DocumentSymbolNode> Services::outline(const Snapshot& snapshot) {
  if(snapshot.module && (snapshot.module->id=="c" || snapshot.module->id=="cpp")) return c_family_outline(snapshot);
  std::vector<DocumentSymbolNode> out;
  if (!snapshot.root) return out;
  const LoadedModule* mod = snapshot.module.get();

  if(mod && mod->id=="python") {
    using Node=lx::uast::GreenNode;
    auto rule=[&](const Node* n){return n&&!n->is_seq()?mod->symbol_name(lx::uast::kind_symbol(n->kind)):std::string_view{};};
    std::function<std::pair<const Node*,uint32_t>(const Node*,uint32_t,std::string_view)> find=
      [&](const Node* n,uint32_t off,std::string_view wanted)->std::pair<const Node*,uint32_t>{
        if(!n)return {};if(rule(n)==wanted)return {n,off};
        if(n->is_inner())for(auto& child:n->as_inner()->children()){auto found=find(child.node,off,wanted);if(found.first)return found;if(child.node)off+=child.node->width;}return {};
      };
    std::function<void(const Node*,uint32_t,std::vector<DocumentSymbolNode>&)> walk=
      [&](const Node* n,uint32_t off,std::vector<DocumentSymbolNode>& dst){
       if(!n||n->is_token()||n->is_error()||n->is_missing())return;
       auto kind=rule(n);bool fn=kind=="function_def"||kind=="function_def_raw",cls=kind=="class_def"||kind=="class_def_raw";
       if(fn||cls){auto raw=find(n,off,fn?"function_def_raw":"class_def_raw");if(!raw.first)raw={n,off};auto id=find(raw.first,raw.second,"NAME");if(!id.first)id=find(raw.first,raw.second,"name");
        if(id.first){DocumentSymbolNode symbol;symbol.kind=fn?12:5;symbol.name=snapshot.rope.slice_text(id.second,id.second+id.first->width);symbol.range={off,off+n->width};symbol.selection_range={id.second,id.second+id.first->width};
         std::function<void(const Node*,uint32_t)> body=[&](const Node* c,uint32_t pos){if(!c)return;auto k=rule(c);if((k=="block"||k=="class_body")&&c!=n){walk(c,pos,symbol.children);return;}if(c->is_inner())for(auto& child:c->as_inner()->children()){body(child.node,pos);if(child.node)pos+=child.node->width;}};body(raw.first,raw.second);
         dst.push_back(std::move(symbol));return;
        }
       }
       if(n->is_inner())for(auto& child:n->as_inner()->children()){walk(child.node,off,dst);if(child.node)off+=child.node->width;}
      };
    walk(snapshot.root.get(),0,out);return out;
  }

  std::string qsrc = get_outline_query(mod);
  if (!qsrc.empty()) {
    if (auto cached = services::QueryCache::global().get_or_compile(qsrc)) {
      auto matches = QueryEngine::execute(*cached->query, snapshot.root.get(), snapshot.rope, mod);
      for (const auto& m : matches) {
        std::optional<lx::uast::Range> item_range;
        std::optional<lx::uast::Range> name_range;
        std::string sym_name;
        int sym_kind = 12;
        for (const auto& c : m.captures) {
          if (c.capture_name == "name" || c.capture_name == "outline.name") {
            name_range = c.range;
            sym_name = snapshot.rope.slice_text(c.range.start, c.range.end);
          } else {
            if (!item_range) item_range = c.range;
            if (c.capture_name == "item" || c.capture_name == "symbol" ||
                c.capture_name == "outline" ||
                c.capture_name.find("definition") != std::string_view::npos) {
              item_range = c.range;
              if (c.node && mod) {
                sym_kind = guess_symbol_kind(mod->symbol_name(lx::uast::kind_symbol(c.node->kind)));
              }
            } else {
              sym_kind = guess_symbol_kind(c.capture_name);
            }
          }
        }
        if (item_range) {
          if (sym_name.empty()) {
            sym_name = snapshot.rope.slice_text(
                item_range->start, std::min(item_range->end, item_range->start + 32u));
          }
          DocumentSymbolNode ds;
          ds.name = std::move(sym_name);
          ds.kind = sym_kind;
          ds.range = *item_range;
          ds.selection_range = name_range.value_or(*item_range);
          out.push_back(std::move(ds));
        }
      }
      if (!out.empty()) return out;
    }
  }

  auto find_first_name_token = [&](const lx::uast::GreenNode* node, uint32_t off)
      -> std::pair<std::string, lx::uast::Range> {
    if (!node || !node->is_inner()) return {{}, {off, off + (node ? node->width : 0)}};
    std::function<std::pair<std::string, lx::uast::Range>(const lx::uast::GreenNode*, uint32_t)> scan =
        [&](const lx::uast::GreenNode* cur_n, uint32_t cur_off)
            -> std::pair<std::string, lx::uast::Range> {
          if (!cur_n || !cur_n->is_inner()) return {{}, {}};
          uint32_t p = cur_off;
          for (const auto& ref : cur_n->as_inner()->children()) {
            if (!ref.node) continue;
            if (ref.node->is_seq()) {
              auto res = scan(ref.node, p);
              if (!res.first.empty()) return res;
            } else if (ref.node->is_token() && !ref.node->is_hidden() && ref.node->width > 0) {
              lx_symbol s = lx::uast::kind_symbol(ref.node->kind);
              std::string_view sname = mod ? mod->symbol_name(s) : std::string_view{};
              std::string lower = to_lower(sname);
              if (lower.find("ident") != std::string::npos || lower == "id" ||
                  lower.find("string") != std::string::npos || lower == "name") {
                return {snapshot.rope.slice_text(p, p + ref.node->width),
                        lx::uast::Range{p, p + ref.node->width}};
              }
            }
            p += ref.node->width;
          }
          return {{}, {}};
        };
    auto res = scan(node, off);
    if (!res.first.empty()) return res;
    return {{}, lx::uast::Range{off, off + node->width}};
  };

  std::function<void(const lx::uast::GreenNode*, uint32_t, bool, std::vector<DocumentSymbolNode>&, int)> walk =
      [&](const lx::uast::GreenNode* node, uint32_t off, bool is_root,
          std::vector<DocumentSymbolNode>& dst, int depth) {
        if (!node || depth > 16) return;
        if (node->is_token() || node->is_error() || node->is_missing()) return;

        bool emit = false;
        if (!is_root && node->is_rule() && !node->is_seq() && node->width > 0) {
          lx_symbol sym = lx::uast::kind_symbol(node->kind);
          uint32_t flags = mod ? mod->symbol_flags(sym) : LX_SYM_NAMED;
          std::string_view sname = mod ? mod->symbol_name(sym) : std::string_view{};
          if ((flags & LX_SYM_NAMED) && !(flags & LX_SYM_HIDDEN) && !sname.empty()) {
            auto [tok_name, sel_range] = find_first_name_token(node, off);
            if (!tok_name.empty()) {
              emit = true;
              DocumentSymbolNode ds;
              ds.name = std::move(tok_name);
              ds.detail = std::string(sname);
              ds.kind = guess_symbol_kind(sname);
              ds.range = lx::uast::Range{off, off + node->width};
              ds.selection_range = sel_range;
              if (node->is_inner()) {
                uint32_t cur = off;
                for (const auto& ref : node->as_inner()->children()) {
                  if (ref.node) {
                    walk(ref.node, cur, false, ds.children, depth + 1);
                    cur += ref.node->width;
                  }
                }
              }
              dst.push_back(std::move(ds));
            }
          }
        }
        if (!emit && node->is_inner()) {
          uint32_t cur = off;
          for (const auto& ref : node->as_inner()->children()) {
            if (ref.node) {
              walk(ref.node, cur, false, dst, depth + (node->is_seq() ? 0 : 1));
              cur += ref.node->width;
            }
          }
        }
      };

  walk(snapshot.root.get(), 0, true, out, 0);
  return out;
}

std::vector<lx::uast::Range> Services::expand_selection(const Snapshot& snapshot,
                                                        uint32_t byte_offset) {
  return expand_selection_tree(snapshot.root.get(), byte_offset);
}

std::vector<lx::uast::Range> Services::expand_selection_tree(const lx::uast::GreenNode* root,
                                                             uint32_t byte_offset) {
  std::vector<lx::uast::Range> out;
  if (!root) return out;
  lx::uast::Cursor cur(root);
  if (!cur.goto_byte(byte_offset)) {
    out.push_back(lx::uast::Range{0, root->width});
    return out;
  }
  do {
    lx::uast::Range r = cur.range();
    if (out.empty() || out.back() != r) {
      out.push_back(r);
    }
  } while (cur.parent());
  return out;
}

std::optional<BracketPair> Services::matching_brackets(const Snapshot& snapshot,
                                                       uint32_t byte_offset) {
  if (!snapshot.root) return std::nullopt;
  lx::uast::Cursor cur(snapshot.root.get());
  if (!cur.goto_byte(byte_offset)) return std::nullopt;
  if (!cur.is_token()) return std::nullopt;

  lx::uast::Range tok_range = cur.range();
  if (tok_range.length() == 0) return std::nullopt;

  auto mirror_of = [](std::string_view s) -> std::pair<std::string_view, bool> {
    if (s == "(") return {")", true};
    if (s == ")") return {"(", false};
    if (s == "{") return {"}", true};
    if (s == "}") return {"{", false};
    if (s == "[") return {"]", true};
    if (s == "]") return {"[", false};
    if (s == "<") return {">", true};
    if (s == ">") return {"<", false};
    return {{}, true};
  };

  std::string tok_txt = snapshot.rope.slice_text(tok_range.start, tok_range.end);
  auto [target_txt, search_forward] = mirror_of(tok_txt);

  // 1. Check standard bracket pairs within parent rule (flattening Seq transparently!)
  if (!target_txt.empty()) {
    lx::uast::Cursor parent_cur = cur.clone();
    if (parent_cur.parent() && parent_cur.first_child()) {
      std::vector<std::pair<lx::uast::Range, std::string>> toks;
      do {
        if (parent_cur.is_token() && !parent_cur.green_node()->is_hidden() &&
            parent_cur.width() > 0 && parent_cur.width() <= 4) {
          lx::uast::Range r = parent_cur.range();
          toks.emplace_back(r, snapshot.rope.slice_text(r.start, r.end));
        }
      } while (parent_cur.next_sibling());

      for (size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].first == tok_range) {
          if (search_forward) {
            for (size_t j = i + 1; j < toks.size(); ++j) {
              if (toks[j].second == target_txt) return BracketPair{tok_range, toks[j].first};
            }
          } else {
            for (size_t j = i; j-- > 0;) {
              if (toks[j].second == target_txt) return BracketPair{toks[j].first, tok_range};
            }
          }
        }
      }
    }

    // Balanced leaf scan across whole tree (Seq-transparent via Cursor::next_leaf/prev_leaf)
    int depth = 1;
    lx::uast::Cursor scan = cur.clone();
    while (search_forward ? scan.next_leaf() : scan.prev_leaf()) {
      if (!scan.is_token() || scan.green_node()->is_hidden()) continue;
      lx::uast::Range r = scan.range();
      if (r.length() == 0 || r.length() > 4) continue;
      std::string s = snapshot.rope.slice_text(r.start, r.end);
      if (s == tok_txt) {
        ++depth;
      } else if (s == target_txt) {
        --depth;
        if (depth == 0) {
          return search_forward ? BracketPair{tok_range, r} : BracketPair{r, tok_range};
        }
      }
    }
    return std::nullopt;
  }

  // 2. ATN rule-paired delimiters (§11 "По ATN: токены, парные в правиле", e.g. begin/end)
  const LoadedModule* mod = snapshot.module.get();
  if (mod && mod->atn) {
    lx::uast::Cursor pcur = cur.clone();
    if (pcur.parent()) {
      lx_symbol psym = lx::uast::kind_symbol(pcur.kind());
      lx_symbol tsym = lx::uast::kind_symbol(cur.kind());
      if (auto paired = find_atn_paired_symbol(*mod->atn, psym, tsym)) {
        const lx_symbol target_sym = paired->first;
        const bool forward = paired->second;
        int depth = 1;
        lx::uast::Cursor scan = cur.clone();
        while (forward ? scan.next_leaf() : scan.prev_leaf()) {
          if (!scan.is_token() || scan.green_node()->is_hidden()) continue;
          lx_symbol s = lx::uast::kind_symbol(scan.kind());
          if (s == tsym) {
            ++depth;
          } else if (s == target_sym) {
            --depth;
            if (depth == 0) {
              lx::uast::Range r = scan.range();
              return forward ? BracketPair{tok_range, r} : BracketPair{r, tok_range};
            }
          }
        }
      }
    }
  }

  return std::nullopt;
}

std::vector<ExpectedToken> Services::expected_tokens(const Snapshot& snapshot,
                                                     uint32_t byte_offset) {
  std::vector<ExpectedToken> out;
  const LoadedModule* mod = snapshot.module.get();
  if (!mod || !mod->info || !mod->info->symbols) return out;

  auto add_sym = [&](lx_symbol sym) {
    if (sym < LX_FIRST_USER_SYMBOL || sym >= mod->info->symbol_count) return;
    const auto& sinfo = mod->info->symbols[sym];
    if (!(sinfo.flags & LX_SYM_TERMINAL) || (sinfo.flags & LX_SYM_HIDDEN)) return;
    for (const auto& existing : out) {
      if (existing.symbol == sym) return;
    }
    out.push_back(ExpectedToken{
        sym,
        std::string(sinfo.name.ptr ? sinfo.name.ptr : "", sinfo.name.len),
    });
  };

  // 1. Include Missing tokens near cursor position
  for (const auto& d : snapshot.diagnostics) {
    if (d.code == "LX_E_MISSING_TOKEN" &&
        d.range.start <= byte_offset + 4 && d.range.end + 4 >= byte_offset) {
      if (snapshot.root) {
        lx::uast::Cursor c(snapshot.root.get());
        if (c.goto_byte(d.range.start)) {
          do {
            if (c.green_node()->is_missing()) {
              add_sym(lx::uast::kind_symbol(c.kind()));
            }
          } while (c.next_leaf() && c.range().start <= d.range.start);
        }
      }
    }
  }

  // 2. Cursor-position ATN FIRST set with rule-call stack (§11)
  if (mod->atn && snapshot.root) {
    const auto& atn = *mod->atn;
    std::vector<ActiveAtnFrame> stack;
    build_active_atn_stack(atn, snapshot.root.get(), 0, byte_offset, stack);
    if (!stack.empty()) {
      collect_first_from_stack(atn, mod->info->symbol_count, stack, add_sym);
    } else if (atn.start_rule() < atn.rules().size()) {
      ActiveAtnFrame root_frame;
      root_frame.rule_id = atn.start_rule();
      uint32_t s0 = atn.rule(atn.start_rule()).start_state;
      root_frame.active_states = epsilon_closure(atn, std::span<const uint32_t>(&s0, 1));
      stack.push_back(std::move(root_frame));
      collect_first_from_stack(atn, mod->info->symbol_count, stack, add_sym);
    }
  }

  // 3. Fallback: terminal tokens from module symbol table
  if (out.empty()) {
    for (uint32_t i = LX_FIRST_USER_SYMBOL; i < mod->info->symbol_count; ++i) {
      add_sym(static_cast<lx_symbol>(i));
    }
  }
  return out;
}

std::vector<QueryMatch> Services::run_query(
    const Snapshot& snapshot,
    std::string_view query_source,
    std::optional<lx::uast::Range> filter_range,
    std::string* out_error) {
  auto cached = services::QueryCache::global().get_or_compile(query_source, out_error);
  if (!cached) {
    return {};
  }
  return QueryEngine::execute(*cached->query, snapshot.root.get(), snapshot.rope,
                              snapshot.module.get(), filter_range);
}

// ============================================================================
// HighlightCache (§5.8, §11)
// ============================================================================

namespace services {

std::vector<SemanticTokenSpan> HighlightCache::get_or_compute(
    const Snapshot& snapshot,
    std::optional<lx::uast::Range> visible_range) {
  if (!snapshot.root) return {};

  if (!visible_range.has_value()) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = cache_.find(CacheKey{snapshot.module_epoch, snapshot.root.get()});
    if (it != cache_.end()) return it->second;
  }

  auto spans = Services::highlight(snapshot, visible_range);

  if (!visible_range.has_value()) {
    std::lock_guard<std::mutex> lock(mu_);
    if (cache_.size() >= max_entries_) cache_.clear();
    cache_[CacheKey{snapshot.module_epoch, snapshot.root.get()}] = spans;
  }
  return spans;
}

std::vector<SemanticTokenSpan> HighlightCache::highlight_incremental(
    const Snapshot& new_snapshot,
    const std::vector<SemanticTokenSpan>& prev_spans,
    const lx::ChangeSet& changes,
    std::span<const lx::uast::Range> changed_ranges,
    std::optional<lx::uast::Range> visible_range) {
  if (!new_snapshot.root) return {};

  auto overlaps_changed = [&](uint32_t s, uint32_t e) {
    for (const auto& cr : changed_ranges) {
      if (cr.empty()) {
        if (s <= cr.start && cr.start <= e) return true;
      } else {
        if (s < cr.end && cr.start < e) return true;
      }
    }
    return false;
  };

  std::vector<SemanticTokenSpan> out;
  out.reserve(prev_spans.size());

  // Keep unchanged spans from prev_spans mapped into new coordinates.
  for (const auto& sp : prev_spans) {
    if (sp.length == 0) continue;
    const uint32_t old_end = sp.offset + sp.length;
    if (!changes.is_clean(sp.offset, old_end)) continue;

    const uint32_t ns = changes.map_pos(sp.offset, lx::Assoc::After);
    const uint32_t ne = changes.map_pos(old_end, lx::Assoc::Before);
    if (ne <= ns || (ne - ns) != sp.length) continue;
    if (overlaps_changed(ns, ne)) continue;

    if (visible_range.has_value()) {
      if (ne <= visible_range->start || ns >= visible_range->end) continue;
    }

    SemanticTokenSpan mapped = sp;
    mapped.offset = ns;
    out.push_back(mapped);
  }

  // Recompute only changed_ranges (clipped to visible_range if specified).
  for (const auto& cr : changed_ranges) {
    if (cr.empty()) continue;
    lx::uast::Range region = cr;
    if (visible_range.has_value()) {
      region.start = std::max(region.start, visible_range->start);
      region.end = std::min(region.end, visible_range->end);
      if (region.empty()) continue;
    }
    auto fresh = Services::highlight(new_snapshot, region);
    out.insert(out.end(), fresh.begin(), fresh.end());
  }

  sort_and_dedup_spans(out);
  return out;
}

void HighlightCache::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  cache_.clear();
}

size_t HighlightCache::size() const noexcept {
  std::lock_guard<std::mutex> lock(mu_);
  return cache_.size();
}

}  // namespace services

}  // namespace lx::host
