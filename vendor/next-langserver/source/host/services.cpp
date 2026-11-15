#include "host/services.hpp"

#include "host/extensions.hpp"
#include "lx/lx_constants.h"

#include <algorithm>
#include <cctype>
#include <functional>

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
  // Strip suffix like ".builtin", ".call", etc.
  auto dot = cap.find('.');
  std::string_view base = (dot == std::string_view::npos) ? cap : cap.substr(0, dot);
  if (base == "namespace" || base == "module") return SemanticTokenType::Namespace;
  if (base == "type" || base == "type_builtin") return SemanticTokenType::Type;
  if (base == "class") return SemanticTokenType::Class;
  if (base == "enum") return SemanticTokenType::Enum;
  if (base == "interface") return SemanticTokenType::Interface;
  if (base == "struct") return SemanticTokenType::Struct;
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
  if (base == "operator") return SemanticTokenType::Operator;
  return std::nullopt;
}

std::optional<SemanticTokenType> classify_symbol_heuristic(std::string_view sym_name,
                                                           uint32_t sym_flags,
                                                           std::string_view text) {
  if (sym_flags & LX_SYM_HIDDEN) {
    std::string lower = to_lower(sym_name);
    if (lower.find("comment") != std::string::npos || lower.find("doc") != std::string::npos) {
      return SemanticTokenType::Comment;
    }
    // Skip plain whitespace
    return std::nullopt;
  }

  std::string lower = to_lower(sym_name);
  if (lower.find("comment") != std::string::npos) return SemanticTokenType::Comment;
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

  // Quoted keyword literals like 'if', 'return', 'true'
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
    auto compiled = compile_sexpr_query(qsrc);
    if (compiled) {
      std::vector<QueryMatch> matches;
      if (rope) {
        matches = QueryEngine::execute(*compiled.query, root, *rope, mod, visible_range);
      } else if (view) {
        matches = QueryEngine::execute(*compiled.query, root, *view, mod, visible_range);
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
              0,
          });
        }
      }
    }
  }

  if (out.empty()) {
    // Generic fallback: walk tokens intersecting visible_range
    std::function<void(const lx::uast::GreenNode*, uint32_t)> walk =
        [&](const lx::uast::GreenNode* node, uint32_t off) {
          if (!node) return;
          uint32_t end = off + node->width;
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

  std::sort(out.begin(), out.end(), [](const SemanticTokenSpan& a, const SemanticTokenSpan& b) {
    if (a.offset != b.offset) return a.offset < b.offset;
    return a.length < b.length;
  });

  // Deduplicate overlapping spans (keep first/innermost)
  std::vector<SemanticTokenSpan> dedup;
  dedup.reserve(out.size());
  uint32_t last_end = 0;
  for (const auto& s : out) {
    if (s.length == 0) continue;
    if (s.offset < last_end) continue;
    dedup.push_back(s);
    last_end = s.offset + s.length;
  }
  return dedup;
}

int guess_symbol_kind(std::string_view name) {
  std::string lower = to_lower(name);
  if (lower.find("class") != std::string::npos) return 5;      // Class
  if (lower.find("struct") != std::string::npos) return 23;    // Struct
  if (lower.find("enum") != std::string::npos) return 10;      // Enum
  if (lower.find("iface") != std::string::npos ||
      lower.find("interface") != std::string::npos) return 11; // Interface
  if (lower.find("func") != std::string::npos || lower == "fn" ||
      lower.find("method") != std::string::npos ||
      lower.find("rule") != std::string::npos) return 12;      // Function
  if (lower.find("pair") != std::string::npos ||
      lower.find("prop") != std::string::npos ||
      lower.find("member") != std::string::npos) return 7;     // Property
  if (lower.find("obj") != std::string::npos) return 19;       // Object
  if (lower.find("arr") != std::string::npos) return 18;       // Array
  return 13;                                                   // Variable
}

}  // namespace

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
    auto compiled = compile_sexpr_query(qsrc);
    if (compiled) {
      auto matches = QueryEngine::execute(*compiled.query, snapshot.root.get(), snapshot.rope, mod);
      for (const auto& m : matches) {
        for (const auto& c : m.captures) {
          if (c.range.empty()) continue;
          auto p0 = snapshot.rope.byte_to_point(c.range.start);
          auto p1 = snapshot.rope.byte_to_point(c.range.end > 0 ? c.range.end - 1 : 0);
          if (p1.line > p0.line) {
            std::string kind = (c.capture_name.find("comment") != std::string_view::npos)
                                   ? "comment"
                                   : "region";
            out.push_back(FoldRange{p0.line, p1.line, std::move(kind)});
          }
        }
      }
    }
  }

  if (out.empty()) {
    // Generic fallback: multi-line non-root Rule nodes & multi-line hidden comment tokens
    std::function<void(const lx::uast::GreenNode*, uint32_t, bool)> walk =
        [&](const lx::uast::GreenNode* node, uint32_t off, bool is_root) {
          if (!node) return;
          if (node->width > 0) {
            auto p0 = snapshot.rope.byte_to_point(off);
            auto p1 = snapshot.rope.byte_to_point(off + node->width - 1);
            if (p1.line <= p0.line) return;  // single-line subtree cannot contain multi-line folds

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
  // Deduplicate identical start_line ranges
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
  std::vector<DocumentSymbolNode> out;
  if (!snapshot.root) return out;
  const LoadedModule* mod = snapshot.module.get();

  std::string qsrc = get_outline_query(mod);
  if (!qsrc.empty()) {
    auto compiled = compile_sexpr_query(qsrc);
    if (compiled) {
      auto matches = QueryEngine::execute(*compiled.query, snapshot.root.get(), snapshot.rope, mod);
      for (const auto& m : matches) {
        std::optional<lx::uast::Range> item_range;
        std::optional<lx::uast::Range> name_range;
        std::string sym_name;
        for (const auto& c : m.captures) {
          if (c.capture_name == "name" || c.capture_name == "outline.name") {
            name_range = c.range;
            sym_name = snapshot.rope.slice_text(c.range.start, c.range.end);
          } else if (c.capture_name == "item" || c.capture_name == "symbol" ||
                     c.capture_name == "outline" ||
                     c.capture_name.find("definition") != std::string_view::npos) {
            item_range = c.range;
          }
        }
        if (item_range) {
          if (sym_name.empty()) {
            sym_name = snapshot.rope.slice_text(
                item_range->start, std::min(item_range->end, item_range->start + 32u));
          }
          DocumentSymbolNode ds;
          ds.name = std::move(sym_name);
          ds.kind = 12;
          ds.range = *item_range;
          ds.selection_range = name_range.value_or(*item_range);
          out.push_back(std::move(ds));
        }
      }
      if (!out.empty()) return out;
    }
  }

  // Generic fallback: hierarchical walk of named non-root Rule nodes
  auto find_first_name_token = [&](const lx::uast::GreenNode* node, uint32_t off)
      -> std::pair<std::string, lx::uast::Range> {
    if (!node || !node->is_inner()) return {{}, {off, off + (node ? node->width : 0)}};
    uint32_t cur = off;
    for (const auto& ref : node->as_inner()->children()) {
      if (!ref.node) continue;
      if (ref.node->is_token() && !ref.node->is_hidden() && ref.node->width > 0) {
        lx_symbol s = lx::uast::kind_symbol(ref.node->kind);
        std::string_view sname = mod ? mod->symbol_name(s) : std::string_view{};
        std::string lower = to_lower(sname);
        if (lower.find("ident") != std::string::npos || lower == "id" ||
            lower.find("string") != std::string::npos || lower == "name") {
          return {snapshot.rope.slice_text(cur, cur + ref.node->width),
                  lx::uast::Range{cur, cur + ref.node->width}};
        }
      }
      cur += ref.node->width;
    }
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
            // Only emit rules that carry a named identifier/string or have multiple children
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
  if (tok_range.length() == 0 || tok_range.length() > 4) return std::nullopt;
  std::string tok_txt = snapshot.rope.slice_text(tok_range.start, tok_range.end);

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

  auto [target, search_forward] = mirror_of(tok_txt);
  if (target.empty()) return std::nullopt;

  // First check siblings within parent rule (O(siblings) per §11 "парные в правиле")
  lx::uast::Cursor parent_cur = cur.clone();
  if (parent_cur.parent()) {
    const lx::uast::GreenNode* pnode = parent_cur.green_node();
    lx::uast::Range prange = parent_cur.range();
    if (pnode && pnode->is_inner()) {
      std::vector<std::pair<lx::uast::Range, std::string>> toks;
      uint32_t off = prange.start;
      for (const auto& ref : pnode->as_inner()->children()) {
        if (!ref.node) continue;
        if (ref.node->is_token() && !ref.node->is_hidden() && ref.node->width > 0 &&
            ref.node->width <= 4) {
          toks.emplace_back(lx::uast::Range{off, off + ref.node->width},
                            snapshot.rope.slice_text(off, off + ref.node->width));
        }
        off += ref.node->width;
      }
      for (size_t i = 0; i < toks.size(); ++i) {
        if (toks[i].first == tok_range) {
          if (search_forward) {
            for (size_t j = i + 1; j < toks.size(); ++j) {
              if (toks[j].second == target) {
                return BracketPair{tok_range, toks[j].first};
              }
            }
          } else {
            for (size_t j = i; j-- > 0;) {
              if (toks[j].second == target) {
                return BracketPair{toks[j].first, tok_range};
              }
            }
          }
        }
      }
    }
  }

  // Fallback: balanced leaf scan from cursor
  int depth = 1;
  lx::uast::Cursor scan = cur.clone();
  while (search_forward ? scan.next_leaf() : scan.prev_leaf()) {
    if (!scan.is_token() || scan.green_node()->is_hidden()) continue;
    lx::uast::Range r = scan.range();
    if (r.length() == 0 || r.length() > 4) continue;
    std::string s = snapshot.rope.slice_text(r.start, r.end);
    if (s == tok_txt) {
      ++depth;
    } else if (s == target) {
      --depth;
      if (depth == 0) {
        return search_forward ? BracketPair{tok_range, r} : BracketPair{r, tok_range};
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

  // Check for Missing nodes near byte_offset
  for (const auto& d : snapshot.diagnostics) {
    if (d.code == "LX_E_MISSING_TOKEN" &&
        d.range.start <= byte_offset + 4 && d.range.end + 4 >= byte_offset) {
      if (snapshot.root) {
        lx::uast::Cursor c(snapshot.root.get());
        if (c.goto_byte(d.range.start)) {
          if (c.green_node()->is_missing()) {
            add_sym(lx::uast::kind_symbol(c.kind()));
          }
        }
      }
    }
  }

  // Compute ATN FIRST set from enclosing rule(s) at cursor position (§11)
  if (mod->atn && snapshot.root) {
    lx::uast::Cursor c(snapshot.root.get());
    uint32_t probe = (byte_offset > 0 && byte_offset >= snapshot.root->width)
                         ? (snapshot.root->width - 1)
                         : byte_offset;
    std::vector<lx_symbol> rule_syms;
    if (c.goto_byte(probe)) {
      do {
        if (c.green_node()->is_rule() && !c.green_node()->is_seq()) {
          rule_syms.push_back(lx::uast::kind_symbol(c.kind()));
        }
      } while (c.parent());
    }

    const auto& atn = *mod->atn;
    auto collect_first_from_state = [&](uint32_t start_state) {
      std::vector<uint32_t> work{start_state};
      std::vector<bool> visited(atn.states().size(), false);
      while (!work.empty()) {
        uint32_t st = work.back();
        work.pop_back();
        if (st >= visited.size() || visited[st]) continue;
        visited[st] = true;
        for (const auto& tr : atn.state_transitions(st)) {
          using TK = lx::atn::TransitionKind;
          switch (tr.kind) {
            case TK::Epsilon:
            case TK::Pred:
            case TK::Precedence:
            case TK::Action:
              work.push_back(tr.to);
              break;
            case TK::Rule:
              work.push_back(tr.arg0);
              break;
            case TK::Atom:
              add_sym(static_cast<lx_symbol>(tr.arg0));
              break;
            case TK::Range:
              for (uint32_t s = tr.arg0; s <= tr.arg1 && s < mod->info->symbol_count; ++s) {
                add_sym(static_cast<lx_symbol>(s));
              }
              break;
            case TK::Set:
              if (tr.arg0 < atn.sets().size()) {
                for (const auto& iv : atn.set_intervals(static_cast<uint16_t>(tr.arg0))) {
                  for (uint32_t s = iv.lo; s <= iv.hi && s < mod->info->symbol_count; ++s) {
                    add_sym(static_cast<lx_symbol>(s));
                  }
                }
              }
              break;
            case TK::NotSet:
            case TK::Wildcard:
              for (uint32_t s = LX_FIRST_USER_SYMBOL; s < mod->info->symbol_count; ++s) {
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
    };

    for (lx_symbol rsym : rule_syms) {
      for (const auto& rule : atn.rules()) {
        if (rule.symbol_id == rsym) {
          collect_first_from_state(rule.start_state);
          break;
        }
      }
      if (!out.empty()) break;
    }
    if (out.empty() && atn.start_rule() < atn.rules().size()) {
      collect_first_from_state(atn.rule(atn.start_rule()).start_state);
    }
  }

  // Fallback: collect terminal tokens from module symbol table
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
  auto compiled = compile_sexpr_query(query_source);
  if (!compiled) {
    if (out_error) *out_error = compiled.error;
    return {};
  }
  return QueryEngine::execute(*compiled.query, snapshot.root.get(), snapshot.rope,
                              snapshot.module.get(), filter_range);
}

}  // namespace lx::host
