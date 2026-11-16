#pragma once

#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "host/module_loader.hpp"
#include "host/query_parser.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lx::host {

struct QueryCapture {
  uint32_t capture_id{0};
  std::string_view capture_name;
  const lx::uast::GreenNode* node{nullptr};
  lx::uast::Range range{};
};

struct QueryMatch {
  uint32_t pattern_index{0};
  std::vector<QueryCapture> captures;
};

/**
 * Executes compiled S-expression queries over any UAST tree + Rope/RopeView (§11).
 * Supports supertype matching (§5.2), field constraints, wildcards, alternations,
 * quantifiers, anchors, and #eq? / #not-eq? / #match? / #not-match? / #any-of?.
 */
class QueryEngine {
 public:
  /**
   * Runs `query` over `root` (positioned at `root_offset` in `rope`), optionally
   * restricting root node candidates to those intersecting `filter_range`.
   */
  static std::vector<QueryMatch> execute(
      const CompiledQuery& query,
      const lx::uast::GreenNode* root,
      const lx::Rope& rope,
      const LoadedModule* mod,
      std::optional<lx::uast::Range> filter_range = std::nullopt,
      uint32_t root_offset = 0);

  /**
   * Overload reading text from a RopeView (for embedded layers §9).
   */
  static std::vector<QueryMatch> execute(
      const CompiledQuery& query,
      const lx::uast::GreenNode* root,
      const lx::RopeView& view,
      const LoadedModule* mod,
      std::optional<lx::uast::Range> filter_range = std::nullopt);
};

}  // namespace lx::host
