#include "host/parser_adapter.hpp"

#include "engine/lex/lexdrv.h"
#include "lx/lx_constants.h"

#ifdef LX_HAS_ENGINE_PARSE
#include "engine/parse/parse.h"
#endif

namespace lx::host {

namespace {

#ifdef LX_HAS_ENGINE_PARSE
ParseOutput run_engine_parse(const ParseInput& in) {
  ParseOutput out;
  if (!in.module || !in.module->vtable || !in.module->atn || (!in.rope && !in.view)) {
    out.status = LX_BAD_ARG;
    out.error = "missing module, ATN, or input";
    return out;
  }

  std::shared_ptr<lx::parse::DfaCache> dfa_cache;
  {
    std::lock_guard lock(in.module->cache_mu);
    if (!in.module->parser_cache) {
      in.module->parser_cache = std::make_shared<lx::parse::DfaCache>(in.module->atn.get());
    }
    dfa_cache = std::static_pointer_cast<lx::parse::DfaCache>(in.module->parser_cache);
  }

  lx::parse::ParseOptions opts;
  opts.lang = in.module->vtable;
  opts.atn = in.module->atn.get();
  opts.lang_idx = in.module->lang_idx;
  opts.state_interner = in.registry ? &in.registry->state_interner() : nullptr;
  opts.token_interner = in.registry ? &in.registry->token_interner() : nullptr;
  opts.dfa_cache = dfa_cache.get();
  opts.limits.max_file_bytes = in.config.max_file_bytes;
  opts.limits.max_tree_depth = in.config.max_tree_depth;
  opts.limits.max_nodes = in.config.max_nodes;
  opts.limits.max_parse_ms = in.config.max_parse_ms;
  opts.limits.max_errors = in.config.max_errors;
  opts.limits.full_reparse_ratio = in.config.full_reparse_ratio;
  opts.cancel_flag = in.cancel ? &in.cancel->cancelled : nullptr;
  opts.expected_grammar_hash = in.module->info ? in.module->info->grammar_hash : 0;
  opts.compute_diff = false;

  lx::parse::ParseResult res;
  if (!in.force_full && in.old_tree && in.changes && !in.changes->empty()) {
    lx::parse::OldSnapshotView old_view;
    old_view.root = in.old_tree;
    old_view.grammar_hash = opts.expected_grammar_hash;
    res = in.view ? lx::parse::reparse(opts, old_view, *in.changes, *in.view)
                  : lx::parse::reparse(opts, old_view, *in.changes, *in.rope);
  } else {
    res = in.view ? lx::parse::parse_full(opts, *in.view)
                  : lx::parse::parse_full(opts, *in.rope);
  }

  out.status = res.status;
  out.root = std::move(res.root);
  out.truncated = res.truncated;
  out.tokens_relexed = res.stats.tokens_lexed + res.stats.tokens_reidentified;
  out.nodes_reused = res.stats.nodes_reused;
  out.nodes_created = res.stats.nodes_created;
  return out;
}
#endif

}  // namespace

ParseOutput lex_only_parse_fallback(const ParseInput& in) {
#ifdef LX_HAS_ENGINE_PARSE
  if (in.module && in.module->atn) {
    return run_engine_parse(in);
  }
#endif
  ParseOutput out;
  if (!in.module || !in.module->vtable || (!in.rope && !in.view)) {
    out.status = LX_BAD_ARG;
    out.error = "missing module or text input";
    return out;
  }

  lx::TokenStreamOptions topts;
  topts.lang = in.module->vtable;
  topts.lang_idx = in.module->lang_idx;
  topts.state_interner = in.registry ? &in.registry->state_interner() : nullptr;
  topts.token_interner = in.registry ? &in.registry->token_interner() : nullptr;
  topts.new_rope = in.rope;
  topts.new_view = in.view;
  if (!in.force_full && in.old_tree && in.changes && !in.changes->empty()) {
    topts.old_tree = in.old_tree;
    topts.changes = in.changes;
  }

  lx::TokenStream stream(topts);
  lx::uast::TreeBuilder builder(topts.token_interner);
  lx_symbol start_sym = LX_FIRST_USER_SYMBOL;
  if (in.module->vtable->parser && in.module->info) {
    uint32_t sr = in.module->vtable->parser->start_rule;
    if (sr < in.module->info->symbol_count) {
      start_sym = static_cast<lx_symbol>(sr);
    }
  }
  builder.begin_node(lx::uast::make_kind(in.module->lang_idx, start_sym), lx::uast::FLAG_RULE);

  uint32_t node_count = 1;
  while (true) {
    if (in.cancel && in.cancel->is_cancelled()) {
      out.status = LX_CANCELLED;
      return out;
    }
    if (node_count >= in.config.max_nodes) {
      out.truncated = true;
      break;
    }

    const auto& tok = stream.peek(0);
    auto hidden = stream.take_hidden();
    for (const auto& h : hidden) {
      if (h.green) {
        builder.add_child(h.green);
        ++node_count;
      }
    }
    if (tok.is_eof()) break;

    lx::LexToken consumed = stream.next();
    if (consumed.green) {
      builder.add_child(consumed.green);
      ++node_count;
    }
  }

  builder.end_node();
  out.root = builder.build();
  out.tokens_relexed = stream.stats().new_tokens + stream.stats().reidentified_tokens;
  out.nodes_created = node_count;
  out.status = stream.last_status();
  if (out.status != LX_OK && out.status != LX_CANCELLED) {
    out.error = "lexer returned error status";
  }
  return out;
}

}  // namespace lx::host
