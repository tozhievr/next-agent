#pragma once

#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "host/limits.hpp"
#include "host/module_loader.hpp"
#include "host/parser_adapter.hpp"
#include "host/services.hpp"
#include "host/snapshot.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lx::host {

struct InjectionKey {
  const lx::uast::GreenNode* parent_green{nullptr};
  std::string language_id;
  bool combined{false};

  bool operator==(const InjectionKey& o) const noexcept {
    return parent_green == o.parent_green && language_id == o.language_id &&
           combined == o.combined;
  }
};

/**
 * Represents an embedded language layer over one or more ranges of the root Rope (§9.1).
 */
struct Layer {
  InjectionKey key;
  std::string language_id;
  std::vector<lx_range> ranges;  // in root Rope coordinates
  lx::RopeView view;
  std::shared_ptr<const LoadedModule> module;
  lx::uast::GreenPtr root;
  std::vector<Diagnostic> diagnostics;
  uint32_t depth{1};
  bool truncated{false};
  std::vector<std::shared_ptr<const Layer>> children;
};

struct InjectionSpec {
  InjectionKey key;
  std::string language_id;
  std::vector<lx_range> ranges;
  bool combined{false};
};

/**
 * Discovers, matches, and incrementally updates embedded language layers (§9.2, §9.3, §9.5).
 */
class LayerManager {
 public:
  /**
   * Discovers embedded language injections in `parent_root` over `parent_rope` (or `parent_view`),
   * optionally restricted to `changed_ranges`.
   */
  static std::vector<InjectionSpec> discover_injections(
      const lx::uast::GreenNode* parent_root,
      const lx::Rope& root_rope,
      const lx::RopeView* parent_view,
      const LoadedModule* parent_module,
      std::span<const lx::uast::Range> changed_ranges = {});

  /**
   * Updates the layer tree after a root (or parent) reparse (§9.2, §9.3, §9.5).
   * Reuses unchanged layers by parent_green_ptr + language_id without reparsing,
   * maps ranges and builds view-local ChangeSets for modified layers, and enforces
   * max_layer_depth and cycle detection.
   */
  static std::vector<std::shared_ptr<const Layer>> update_layers(
      const lx::uast::GreenNode* parent_root,
      const lx::Rope& root_rope,
      const lx::RopeView* parent_view,
      const LoadedModule* parent_module,
      const std::vector<std::shared_ptr<const Layer>>& prev_layers,
      const lx::ChangeSet& root_changes,
      std::span<const lx::uast::Range> changed_ranges,
      LanguageRegistry& registry,
      const ParseBackendFn& parse_fn,
      const HostConfig& config,
      uint32_t depth = 1,
      const std::vector<std::pair<std::string, lx_range>>& ancestor_chain = {});
};

/**
 * Combined cursor navigating across the root snapshot and nested embedded layers (§9.4).
 */
class LayeredCursor {
 public:
  LayeredCursor(std::shared_ptr<const Snapshot> snapshot,
                std::vector<std::shared_ptr<const Layer>> layers);

  /**
   * Moves to the deepest layer containing `doc_byte` and positions the cursor on the
   * deepest non-Seq node covering `doc_byte` (§9.4).
   */
  bool goto_byte(uint32_t doc_byte);

  const Layer* current_layer() const noexcept { return current_layer_.get(); }
  bool in_embedded_layer() const noexcept { return current_layer_ != nullptr; }
  std::string_view language_id() const noexcept;
  const LoadedModule* module() const noexcept;

  lx::uast::Cursor& cursor() noexcept { return cursor_; }
  const lx::uast::Cursor& cursor() const noexcept { return cursor_; }

  /**
   * Returns current node range in root document byte coordinates.
   */
  lx::uast::Range doc_range() const noexcept;

  /**
   * Collects diagnostics across root snapshot and all nested layers (mapped to root doc coords).
   */
  std::vector<Diagnostic> all_diagnostics() const;

  /**
   * Collects semantic tokens across root snapshot and all nested layers (mapped to root doc coords).
   */
  std::vector<SemanticTokenSpan> all_semantic_tokens(
      std::optional<lx::uast::Range> visible_range = std::nullopt) const;

 private:
  std::shared_ptr<const Snapshot> snapshot_;
  std::vector<std::shared_ptr<const Layer>> layers_;
  std::shared_ptr<const Layer> current_layer_;
  lx::uast::Cursor cursor_;
};

}  // namespace lx::host
