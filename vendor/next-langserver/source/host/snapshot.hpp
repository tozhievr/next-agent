#pragma once

#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "host/module_loader.hpp"
#include "lx/lx_language.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lx::host {

enum class DiagnosticSeverity : uint8_t {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4,
};

struct Diagnostic {
  std::string code;
  std::string message;
  lx_range range{0, 0};
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
};

/**
 * Immutable thread-safe document snapshot (§3.1, §6.6, §10.3).
 * Holds a shared_ptr to LoadedModule so the DLL cannot be unloaded while
 * any snapshot of its language is alive.
 */
struct Snapshot {
  uint64_t version{0};
  uint64_t module_epoch{0};
  uint64_t grammar_hash{0};
  uint32_t lang_version{0};
  lx::Rope rope;
  lx::uast::GreenPtr root;
  std::vector<lx::uast::Range> changed_ranges;
  std::vector<Diagnostic> diagnostics;
  std::shared_ptr<const LoadedModule> module;
  bool truncated{false};
};

/**
 * Extracts diagnostics from Error / Missing / Truncated nodes in `root`.
 * Skips clean subtrees in O(errors * tree_depth) by checking FLAG_HAS_ERROR.
 */
std::vector<Diagnostic> extract_diagnostics(const lx::uast::GreenNode* root,
                                            const LoadedModule* mod,
                                            const lx::Rope* rope = nullptr,
                                            uint32_t base_offset = 0);

}  // namespace lx::host
