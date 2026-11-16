#include "host/query_parser.hpp"
#include "host/services/query_cache.hpp"

#include <cctype>

namespace lx::host {

std::optional<uint32_t> CompiledQuery::find_capture_id(std::string_view name) const {
  for (size_t i = 0; i < capture_names.size(); ++i) {
    if (capture_names[i] == name) return static_cast<uint32_t>(i);
  }
  return std::nullopt;
}

namespace {

class SexprParser {
 public:
  explicit SexprParser(std::string_view src) : src_(src) {}

  QueryCompileResult parse(std::vector<services::QuerySetDirective>* out_sets = nullptr) {
    CompiledQuery q;
    while (true) {
      skip_ws_and_comments();
      if (pos_ >= src_.size()) break;

      auto node = parse_element(q, /*allow_field=*/false);
      if (!node) return {std::nullopt, std::move(err_), pos_};

      if (node->kind == QueryNode::Kind::Group && !node->symbol_name.empty() &&
          node->symbol_name[0] == '#') {
        if (!q.patterns.empty()) {
          auto& last = q.patterns.back();
          handle_predicate_node(*node, last.pattern_index, last.predicates, out_sets);
        } else {
          std::vector<QueryPredicate> unused;
          handle_predicate_node(*node, 0, unused, out_sets);
        }
        continue;
      }

      QueryPattern pat;
      pat.pattern_index = static_cast<uint32_t>(q.patterns.size());
      pat.root = std::move(*node);
      extract_predicates_recursive(pat.root, pat.pattern_index, pat.predicates, out_sets);
      q.patterns.push_back(std::move(pat));
    }
    return {std::move(q), {}, 0};
  }

 private:
  std::string_view src_;
  size_t pos_{0};
  std::string err_;

  void fail(std::string msg) {
    if (err_.empty()) err_ = std::move(msg);
  }

  void skip_ws_and_comments() {
    while (pos_ < src_.size()) {
      char c = src_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos_;
      } else if (c == ';') {
        while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
  }

  static bool is_ident_char(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || c == '.' ||
           c == '$' || c == '#' || c == '?' || c == '!';
  }

  std::string parse_ident() {
    size_t start = pos_;
    while (pos_ < src_.size() && is_ident_char(src_[pos_])) {
      ++pos_;
    }
    return std::string(src_.substr(start, pos_ - start));
  }

  std::optional<std::string> parse_string() {
    if (pos_ >= src_.size() || src_[pos_] != '"') {
      fail("expected '\"'");
      return std::nullopt;
    }
    ++pos_;
    std::string out;
    while (pos_ < src_.size()) {
      char c = src_[pos_++];
      if (c == '"') return out;
      if (c == '\\') {
        if (pos_ >= src_.size()) {
          fail("unterminated escape in query string");
          return std::nullopt;
        }
        char esc = src_[pos_++];
        switch (esc) {
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case '\\': out.push_back('\\'); break;
          case '"': out.push_back('"'); break;
          default: out.push_back(esc); break;
        }
      } else {
        out.push_back(c);
      }
    }
    fail("unterminated string literal in query");
    return std::nullopt;
  }

  uint32_t intern_capture(CompiledQuery& q, std::string name) {
    for (size_t i = 0; i < q.capture_names.size(); ++i) {
      if (q.capture_names[i] == name) return static_cast<uint32_t>(i);
    }
    uint32_t id = static_cast<uint32_t>(q.capture_names.size());
    q.capture_names.push_back(std::move(name));
    return id;
  }

  void parse_trailing_modifiers(CompiledQuery& q, QueryNode& node) {
    while (true) {
      skip_ws_and_comments();
      if (pos_ >= src_.size()) break;
      char c = src_[pos_];
      if (c == '?' || c == '*' || c == '+') {
        if (c == '?') node.quantifier = QueryQuantifier::Optional;
        else if (c == '*') node.quantifier = QueryQuantifier::ZeroOrMore;
        else node.quantifier = QueryQuantifier::OneOrMore;
        ++pos_;
      } else if (c == '@') {
        ++pos_;
        std::string cap = parse_ident();
        if (cap.empty()) {
          fail("empty capture name after '@'");
          return;
        }
        node.capture_ids.push_back(intern_capture(q, std::move(cap)));
      } else {
        break;
      }
    }
  }

  std::optional<QueryNode> parse_element(CompiledQuery& q, bool allow_field) {
    skip_ws_and_comments();
    if (pos_ >= src_.size()) {
      fail("unexpected end of query");
      return std::nullopt;
    }

    std::string field;
    if (allow_field && is_ident_char(src_[pos_]) &&
        src_[pos_] != '.' && src_[pos_] != '_' && src_[pos_] != '!') {
      size_t save = pos_;
      std::string maybe = parse_ident();
      skip_ws_and_comments();
      if (pos_ < src_.size() && src_[pos_] == ':') {
        ++pos_;
        field = std::move(maybe);
        skip_ws_and_comments();
      } else {
        pos_ = save;
      }
    }

    if (pos_ >= src_.size()) {
      fail("unexpected end after field:");
      return std::nullopt;
    }

    char c = src_[pos_];
    QueryNode node;
    node.field_name = std::move(field);

    if (c == '(') {
      ++pos_;
      skip_ws_and_comments();
      if (pos_ >= src_.size()) {
        fail("unterminated '('");
        return std::nullopt;
      }
      if (src_[pos_] == '(' || src_[pos_] == '[' || src_[pos_] == '"' || src_[pos_] == '.') {
        node.kind = QueryNode::Kind::Group;
      } else if (src_[pos_] == '#') {
        node.kind = QueryNode::Kind::Group;
        node.symbol_name = parse_ident();
        while (true) {
          skip_ws_and_comments();
          if (pos_ >= src_.size()) {
            fail("unterminated predicate");
            return std::nullopt;
          }
          if (src_[pos_] == ')') {
            ++pos_;
            return node;
          }
          if (src_[pos_] == '@') {
            ++pos_;
            std::string cap = parse_ident();
            if (cap.empty()) {
              fail("empty capture name in predicate");
              return std::nullopt;
            }
            QueryNode arg;
            arg.kind = QueryNode::Kind::Named;
            arg.symbol_name = "@" + cap;
            arg.capture_ids.push_back(intern_capture(q, std::move(cap)));
            node.children.push_back(std::move(arg));
          } else if (src_[pos_] == '"') {
            auto s = parse_string();
            if (!s) return std::nullopt;
            QueryNode arg;
            arg.kind = QueryNode::Kind::Anonymous;
            arg.symbol_name = std::move(*s);
            node.children.push_back(std::move(arg));
          } else {
            std::string tok = parse_ident();
            if (tok.empty()) {
              fail("invalid token in predicate");
              return std::nullopt;
            }
            QueryNode arg;
            arg.kind = QueryNode::Kind::Anonymous;
            arg.symbol_name = std::move(tok);
            node.children.push_back(std::move(arg));
          }
        }
      } else {
        std::string name = parse_ident();
        if (name.empty()) {
          fail("expected node kind after '('");
          return std::nullopt;
        }
        if (name == "_") {
          node.kind = QueryNode::Kind::Wildcard;
        } else {
          node.kind = QueryNode::Kind::Named;
          node.symbol_name = std::move(name);
        }
      }

      bool pending_anchor = false;
      while (true) {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
          fail("unterminated '('");
          return std::nullopt;
        }
        if (src_[pos_] == ')') {
          ++pos_;
          if (pending_anchor && !node.children.empty()) {
            node.children.back().anchor_after = true;
          }
          break;
        }
        if (src_[pos_] == '.') {
          ++pos_;
          pending_anchor = true;
          continue;
        }
        if (src_[pos_] == '!') {
          ++pos_;
          std::string neg = parse_ident();
          if (neg.empty()) {
            fail("expected field name after '!'");
            return std::nullopt;
          }
          QueryNode n;
          n.negated_field = std::move(neg);
          node.children.push_back(std::move(n));
          pending_anchor = false;
          continue;
        }
        auto child = parse_element(q, /*allow_field=*/true);
        if (!child) return std::nullopt;
        if (pending_anchor) {
          child->anchor_before = true;
          pending_anchor = false;
        }
        node.children.push_back(std::move(*child));
      }
      parse_trailing_modifiers(q, node);
      if (!err_.empty()) return std::nullopt;
      return node;
    }

    if (c == '[') {
      ++pos_;
      node.kind = QueryNode::Kind::Alternation;
      while (true) {
        skip_ws_and_comments();
        if (pos_ >= src_.size()) {
          fail("unterminated '['");
          return std::nullopt;
        }
        if (src_[pos_] == ']') {
          ++pos_;
          break;
        }
        auto alt = parse_element(q, /*allow_field=*/false);
        if (!alt) return std::nullopt;
        node.children.push_back(std::move(*alt));
      }
      parse_trailing_modifiers(q, node);
      if (!err_.empty()) return std::nullopt;
      return node;
    }

    if (c == '"') {
      auto lit = parse_string();
      if (!lit) return std::nullopt;
      node.kind = QueryNode::Kind::Anonymous;
      node.symbol_name = std::move(*lit);
      parse_trailing_modifiers(q, node);
      if (!err_.empty()) return std::nullopt;
      return node;
    }

    if (c == '_') {
      ++pos_;
      node.kind = QueryNode::Kind::Wildcard;
      parse_trailing_modifiers(q, node);
      if (!err_.empty()) return std::nullopt;
      return node;
    }

    fail(std::string("unexpected token in query: ") + c);
    return std::nullopt;
  }

  void handle_predicate_node(
      const QueryNode& ch,
      uint32_t pat_idx,
      std::vector<QueryPredicate>& out,
      std::vector<services::QuerySetDirective>* out_sets) {
    if (ch.symbol_name == "#set!") {
      if (out_sets && ch.children.size() >= 2) {
        out_sets->push_back(services::QuerySetDirective{
            pat_idx,
            ch.children[0].symbol_name,
            ch.children[1].symbol_name,
        });
      }
      return;
    }
    QueryPredicate pred;
    if (ch.symbol_name == "#eq?") pred.op = QueryPredOp::Eq;
    else if (ch.symbol_name == "#not-eq?") pred.op = QueryPredOp::NotEq;
    else if (ch.symbol_name == "#match?") pred.op = QueryPredOp::Match;
    else if (ch.symbol_name == "#not-match?") pred.op = QueryPredOp::NotMatch;
    else if (ch.symbol_name == "#any-of?") pred.op = QueryPredOp::AnyOf;
    else return;

    for (const auto& arg : ch.children) {
      QueryPredicateArg pa;
      if (!arg.capture_ids.empty()) {
        pa.is_capture = true;
        pa.capture_idx = arg.capture_ids[0];
        pa.value = arg.symbol_name;
      } else {
        pa.is_capture = false;
        pa.value = arg.symbol_name;
      }
      pred.args.push_back(std::move(pa));
    }
    out.push_back(std::move(pred));
  }

  void extract_predicates_recursive(
      QueryNode& node,
      uint32_t pat_idx,
      std::vector<QueryPredicate>& out,
      std::vector<services::QuerySetDirective>* out_sets) {
    std::vector<QueryNode> kept;
    kept.reserve(node.children.size());
    for (auto& ch : node.children) {
      if (ch.kind == QueryNode::Kind::Group && !ch.symbol_name.empty() &&
          ch.symbol_name[0] == '#') {
        handle_predicate_node(ch, pat_idx, out, out_sets);
      } else {
        extract_predicates_recursive(ch, pat_idx, out, out_sets);
        kept.push_back(std::move(ch));
      }
    }
    node.children = std::move(kept);
  }
};

}  // namespace

QueryCompileResult compile_sexpr_query(std::string_view source) {
  SexprParser parser(source);
  return parser.parse();
}

namespace services {

std::vector<QuerySetDirective> extract_set_directives(std::string_view source) {
  std::vector<QuerySetDirective> sets;
  SexprParser parser(source);
  (void)parser.parse(&sets);
  return sets;
}

std::shared_ptr<const CachedQueryEntry> QueryCache::get_or_compile(
    std::string_view source,
    std::string* out_error) {
  {
    std::shared_lock lock(mu_);
    auto it = entries_.find(std::string(source));
    if (it != entries_.end()) {
      return it->second;
    }
  }

  std::vector<QuerySetDirective> sets;
  SexprParser parser(source);
  auto res = parser.parse(&sets);
  if (!res) {
    if (out_error) *out_error = res.error;
    return nullptr;
  }

  auto entry = std::make_shared<CachedQueryEntry>();
  entry->query = std::make_shared<CompiledQuery>(std::move(*res.query));
  entry->set_directives = std::move(sets);

  std::unique_lock lock(mu_);
  if (entries_.size() >= max_entries_) {
    entries_.clear();
  }
  auto [it, _] = entries_.try_emplace(std::string(source), std::move(entry));
  return it->second;
}

void QueryCache::clear() {
  std::unique_lock lock(mu_);
  entries_.clear();
}

size_t QueryCache::size() const noexcept {
  std::shared_lock lock(mu_);
  return entries_.size();
}

QueryCache& QueryCache::global() {
  static QueryCache instance;
  return instance;
}

}  // namespace services

}  // namespace lx::host
