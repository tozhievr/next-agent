#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/atn/atn.h"
#include "engine/lex/lexdrv.h"
#include "engine/parse/parse.h"

namespace lx::parse::internal {

struct ActiveCallerFrame {
  uint32_t return_state = atn::kIndexNoneU32;
  uint16_t prec = 0;
};

struct PredictOutcome {
  int32_t alt = -1; // >= 0: chosen alt index; -1: no viable alt (syntax error)
  uint32_t used_ctx_depth = 0;
  bool used_ll1 = false;
  bool used_llk = false;
  bool used_sll_hit = false;
  bool used_sll_miss = false;
  bool used_full_ll = false;
};

class LookaheadAdapter {
 public:
  virtual ~LookaheadAdapter() = default;
  virtual const LexToken& peek(uint32_t k) = 0;
  virtual int eval_predicate(uint32_t pred_id, uint32_t pred_flags, uint32_t k_offset) = 0;
  virtual bool is_cancelled() const noexcept = 0;
};

/// Runs adaptive ALL(*) prediction for `decision_state_id`:
/// 1) LL(1) table if LX_DEC_LL1
/// 2) LL(k) compact DFA if LX_DEC_LLK
/// 3) Thread-safe SLL DFA cache (when no predicates / precedence guards)
/// 4) Full LL with caller context, precedence, and predicates
PredictOutcome predict_decision(
    const atn::Atn& atn,
    DfaCache* dfa_cache,
    uint32_t decision_state_id,
    uint16_t current_prec,
    std::span<const ActiveCallerFrame> caller_stack,
    LookaheadAdapter& la);

/// Quick 1-token reachability check from `start_state` (for Missing token recovery §6.5(a)).
bool can_reach_symbol_in_one_step(
    const atn::Atn& atn,
    uint32_t start_state,
    uint16_t current_prec,
    std::span<const ActiveCallerFrame> caller_stack,
    lx_symbol target_sym,
    uint32_t* out_used_ctx_depth = nullptr);

/// Returns true iff `sym` is in `atn`'s sync set `set_id` (or is LX_SYMBOL_EOF).
bool symbol_in_sync_set(
    const atn::Atn& atn,
    uint16_t sync_set_id,
    lx_symbol sym) noexcept;

} // namespace lx::parse::internal
