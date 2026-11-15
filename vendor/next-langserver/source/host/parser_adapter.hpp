#pragma once

#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "host/limits.hpp"
#include "host/module_loader.hpp"
#include "host/scheduler.hpp"
#include "lx/lx_language.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace lx::host {

struct ParseInput {
  const LoadedModule* module{nullptr};
  LanguageRegistry* registry{nullptr};
  const lx::Rope* rope{nullptr};         // non-null for root layer
  const lx::RopeView* view{nullptr};     // non-null for embedded layer
  const lx::uast::GreenNode* old_tree{nullptr};
  const lx::ChangeSet* changes{nullptr};
  uint64_t env_version{0};
  const CancelToken* cancel{nullptr};
  HostConfig config{};
  bool force_full{false};
};

struct ParseOutput {
  lx_status status{LX_OK};
  lx::uast::GreenPtr root;
  bool truncated{false};
  uint64_t tokens_relexed{0};
  uint64_t nodes_reused{0};
  uint64_t nodes_created{0};
  std::string error;
};

using ParseBackendFn = std::function<ParseOutput(const ParseInput&)>;

/**
 * Fallback/lex-only parse backend used before engine/parse is bound or when a
 * language module provides only a lexer without parser tables.
 */
ParseOutput lex_only_parse_fallback(const ParseInput& in);

}  // namespace lx::host
