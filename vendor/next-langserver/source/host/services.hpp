#pragma once

#include "host/query_engine.hpp"
#include "host/snapshot.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lx::host {

// Standard LSP semantic token legend types
enum class SemanticTokenType : uint32_t {
  Namespace = 0,
  Type = 1,
  Class = 2,
  Enum = 3,
  Interface = 4,
  Struct = 5,
  TypeParameter = 6,
  Parameter = 7,
  Variable = 8,
  Property = 9,
  EnumMember = 10,
  Event = 11,
  Function = 12,
  Method = 13,
  Macro = 14,
  Keyword = 15,
  Modifier = 16,
  Comment = 17,
  String = 18,
  Number = 19,
  Regexp = 20,
  Operator = 21,
};

inline constexpr const char* kSemanticTokenLegendTypes[] = {
    "namespace", "type", "class", "enum", "interface", "struct",
    "typeParameter", "parameter", "variable", "property", "enumMember", "event",
    "function", "method", "macro", "keyword", "modifier", "comment",
    "string", "number", "regexp", "operator",
};

inline constexpr const char* kSemanticTokenLegendModifiers[] = {
    "declaration", "definition", "readonly", "static", "deprecated",
};

struct SemanticTokenSpan {
  uint32_t offset{0};
  uint32_t length{0};
  uint32_t token_type{0};
  uint32_t modifiers{0};
};

struct FoldRange {
  uint32_t start_line{0};
  uint32_t end_line{0};
  std::string kind;  // "comment", "region"
};

struct DocumentSymbolNode {
  std::string name;
  std::string detail;
  int kind{13};  // LSP SymbolKind (13 = Variable, 12 = Function, 5 = Class, 19 = Object, etc.)
  lx::uast::Range range{};
  lx::uast::Range selection_range{};
  std::vector<DocumentSymbolNode> children;
};

struct BracketPair {
  lx::uast::Range open{};
  lx::uast::Range close{};
};

struct ExpectedToken {
  lx_symbol symbol{0};
  std::string name;
};

/**
 * Stateless read-only services operating on immutable Snapshots (§11).
 */
class Services {
 public:
  /**
   * Computes semantic token spans for `snapshot` (optionally restricted to `visible_range`).
   */
  static std::vector<SemanticTokenSpan> highlight(
      const Snapshot& snapshot,
      std::optional<lx::uast::Range> visible_range = std::nullopt);

  /**
   * Overload for embedded/layered trees over a RopeView (offsets in view coordinates).
   */
  static std::vector<SemanticTokenSpan> highlight_view(
      const lx::uast::GreenNode* root,
      const lx::RopeView& view,
      const LoadedModule* mod,
      std::optional<lx::uast::Range> visible_range = std::nullopt);

  /**
   * Encodes SemanticTokenSpans (in document byte coordinates) into LSP delta-encoded uint32 array.
   * Splits multi-line tokens per LSP specification.
   */
  static std::vector<uint32_t> encode_lsp_semantic_tokens(
      const lx::Rope& rope,
      const std::vector<SemanticTokenSpan>& spans);

  /**
   * Computes folding ranges for `snapshot`.
   */
  static std::vector<FoldRange> fold(const Snapshot& snapshot);

  /**
   * Computes hierarchical document symbols (outline) for `snapshot`.
   */
  static std::vector<DocumentSymbolNode> outline(const Snapshot& snapshot);

  /**
   * Returns chain of expanding UAST ranges starting at `byte_offset`,
   * skipping transparent Seq nodes (§11).
   */
  static std::vector<lx::uast::Range> expand_selection(const Snapshot& snapshot,
                                                       uint32_t byte_offset);

  static std::vector<lx::uast::Range> expand_selection_tree(const lx::uast::GreenNode* root,
                                                            uint32_t byte_offset);

  /**
   * Finds matching bracket/delimiter pair at `byte_offset` (§11).
   */
  static std::optional<BracketPair> matching_brackets(const Snapshot& snapshot,
                                                      uint32_t byte_offset);

  /**
   * Computes expected tokens at `byte_offset` using Missing/Error nodes and
   * grammar terminals/rule context (§11).
   */
  static std::vector<ExpectedToken> expected_tokens(const Snapshot& snapshot,
                                                    uint32_t byte_offset);

  /**
   * Compiles and executes an S-expression query over `snapshot` (§11).
   */
  static std::vector<QueryMatch> run_query(
      const Snapshot& snapshot,
      std::string_view query_source,
      std::optional<lx::uast::Range> filter_range = std::nullopt,
      std::string* out_error = nullptr);
};

}  // namespace lx::host
