#pragma once

#include "engine/uast/uast.h"

namespace lx::uast::internal {

uint32_t mix_u32(uint32_t h, uint32_t v) noexcept;
uint32_t pow_u32(uint32_t base, uint32_t exp) noexcept;
uint32_t hash_non_seq_ref(const GreenRef& ref) noexcept;

void compute_inner_metrics(GreenInner* out, std::span<const GreenRef> children,
                           uint32_t extra_reach_ahead) noexcept;

GreenPtr allocate_token_raw(Kind kind, uint32_t width, uint32_t reach_ahead,
                            uint32_t flags, uint32_t text_hash,
                            uint32_t lex_state_in, uint32_t lex_state_out);

GreenPtr allocate_inner_raw(Kind kind, uint32_t flags, uint32_t seq_height,
                            std::span<const GreenRef> children,
                            uint32_t ctx_depth, uint32_t ctx_hash,
                            uint32_t prec, uint32_t pred_deps_hash,
                            uint32_t extra_reach_ahead);

} // namespace lx::uast::internal
