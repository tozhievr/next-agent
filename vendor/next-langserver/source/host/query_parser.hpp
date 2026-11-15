#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lx::host {

enum class QueryQuantifier : uint8_t {
  One = 0,
  Optional,   // ?
  ZeroOrMore, // *
  OneOrMore,  // +
};

enum class QueryPredOp : uint8_t {
  Eq,         // #eq?
  NotEq,      // #not-eq?
  Match,      // #match?
  NotMatch,   // #not-match?
  AnyOf,      // #any-of?
};

struct QueryPredicateArg {
  bool is_capture{false};
  std::string value;          // capture name (without '@') or string literal
  uint32_t capture_idx{0};
};

struct QueryPredicate {
  QueryPredOp op{QueryPredOp::Eq};
  std::vector<QueryPredicateArg> args;
};

/**
 * A node in a compiled tree-sitter-style S-expression query AST.
 */
struct QueryNode {
  enum class Kind : uint8_t {
    Named,        // (rule_name ...)
    Anonymous,    // "keyword"
    Wildcard,     // (_) or _
    Alternation,  // [ alt1 alt2 ... ]
    Group,        // ( elem1 elem2 ... )
  };

  Kind kind{Kind::Named};
  std::string symbol_name;          // for Named / Anonymous
  std::string field_name;           // optional `field:` prefix
  std::string negated_field;        // optional `!field`
  bool anchor_before{false};        // `.` before this sibling
  bool anchor_after{false};         // `.` after this sibling
  QueryQuantifier quantifier{QueryQuantifier::One};
  std::vector<uint32_t> capture_ids;
  std::vector<QueryNode> children;
};

struct QueryPattern {
  uint32_t pattern_index{0};
  QueryNode root;
  std::vector<QueryPredicate> predicates;
};

struct CompiledQuery {
  std::vector<std::string> capture_names;
  std::vector<QueryPattern> patterns;

  std::optional<uint32_t> find_capture_id(std::string_view name) const;
};

struct QueryCompileResult {
  std::optional<CompiledQuery> query;
  std::string error;
  size_t error_pos{0};

  explicit operator bool() const { return query.has_value(); }
};

/**
 * Compiles tree-sitter-compatible S-expression patterns into a CompiledQuery.
 * Supports:
 * - `(node_kind field: (child) @cap)`
 * - `_` / `(_)` wildcard
 * - `"literal"` anonymous/terminal text match
 * - `[alt1 alt2]` alternation
 * - `(a b)` grouping
 * - quantifiers `*`, `+`, `?`
 * - anchors `.` and negated fields `!field`
 * - predicates `(#eq? @a "x")`, `(#not-eq? @a @b)`, `(#match? @a "re")`,
 *   `(#not-match? @a "re")`, `(#any-of? @a "x" "y")`
 */
QueryCompileResult compile_sexpr_query(std::string_view source);

}  // namespace lx::host
