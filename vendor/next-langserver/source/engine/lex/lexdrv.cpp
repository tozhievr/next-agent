#include "lexdrv.h"

#include <algorithm>
#include <cassert>

namespace lx {

namespace {

constexpr uint32_t kMaxZeroWidthSteps = 1024;

inline uint32_t fnv1a_step(uint32_t h, const uint8_t* data, uint32_t len) noexcept {
  for (uint32_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

}  // namespace

TokenStream::TokenStream(TokenStreamOptions opts)
    : opts_(std::move(opts)) {
  if (opts_.state_interner) {
    state_interner_ = opts_.state_interner;
  } else {
    owned_state_interner_ = std::make_unique<StateInterner>();
    state_interner_ = owned_state_interner_.get();
  }

  if (opts_.lang && opts_.lang->info) {
    lang_info_ = opts_.lang->info();
  }

  if (opts_.new_view) {
    input_ = std::make_unique<RopeInput>(*opts_.new_view);
  } else if (opts_.new_rope) {
    input_ = std::make_unique<RopeInput>(*opts_.new_rope);
  }
  text_length_ = input_ ? input_->length() : 0;

  init_hunks();
  init_start_mode();
}

TokenStream::~TokenStream() = default;

void TokenStream::init_hunks() {
  old_length_ = opts_.old_tree ? opts_.old_tree->width : 0;
  hunks_.clear();
  if (!opts_.old_tree || !opts_.changes || opts_.changes->empty()) {
    return;
  }
  const auto raw_hunks = opts_.changes->hunks();
  hunks_.reserve(raw_hunks.size());
  int64_t cum_delta = 0;
  for (const Hunk& h : raw_hunks) {
    const uint32_t new_start =
        static_cast<uint32_t>(static_cast<int64_t>(h.old_start) + cum_delta);
    const uint32_t new_end = new_start + h.new_len;
    cum_delta += static_cast<int64_t>(h.new_len) -
                 static_cast<int64_t>(h.old_end - h.old_start);
    hunks_.push_back(OldHunkInfo{h.old_start, h.old_end, new_start, new_end, cum_delta});
  }
}

void TokenStream::init_start_mode() {
  raw_new_pos_ = opts_.start_offset;
  raw_state_id_ = state_interner_->intern_state(opts_.initial_state);
  next_hunk_idx_ = 0;
  dirty_old_end_ = 0;

  if (raw_new_pos_ > text_length_) {
    raw_new_pos_ = text_length_;
  }

  if (!opts_.old_tree) {
    // Reaching the byte boundary does not imply EOF: layout lexers may
    // still emit zero-width DEDENT/virtual-brace tokens from their state.
    raw_mode_ = RawMode::Lexing;
    return;
  }

  // Incremental mode with old_tree:
  if (opts_.start_offset == 0 && raw_state_id_ == 0) {
    if (old_length_ == 0 && hunks_.empty()) {
      raw_mode_ = RawMode::AtEof;
      return;
    }
    if (old_length_ == 0 && !hunks_.empty()) {
      // Old file was empty, new text inserted at 0.
      raw_mode_ = (text_length_ == 0) ? RawMode::AtEof : RawMode::Lexing;
      dirty_old_end_ = hunks_[0].old_end;
      return;
    }
    raw_mode_ = RawMode::Synced;
    raw_old_pos_ = 0;
    next_hunk_idx_ = 0;
    if (!sync_cursor_to_old_offset(0)) {
      raw_mode_ = RawMode::Lexing;
    } else {
      maybe_leave_sync_for_next_hunk();
    }
    return;
  }

  // Non-zero start_offset with old_tree: try syncing at start_offset, else lex.
  raw_mode_ = RawMode::Lexing;
  size_t hunk_after = 0;
  if (auto p_old = unmap_after_hunk(raw_new_pos_, hunk_after)) {
    next_hunk_idx_ = hunk_after;
    if (*p_old < old_length_ && sync_cursor_to_old_offset(*p_old)) {
      const uast::GreenToken* tok = sync_cursor_.as_token();
      while (tok && tok->lex_state_in != raw_state_id_ && tok->width == 0) {
        if (!sync_cursor_.next_leaf()) break;
        advance_sync_cursor_past_missing();
        if (!sync_cursor_.valid() || sync_cursor_.range().start != *p_old) {
          tok = nullptr;
          break;
        }
        tok = sync_cursor_.as_token();
      }
      if (tok && tok->lex_state_in == raw_state_id_ &&
          is_clean_for_hunks_from(*p_old, tok->width, tok->reach_ahead, hunk_after)) {
        raw_mode_ = RawMode::Synced;
        raw_old_pos_ = *p_old;
        return;
      }
    }
  } else {
    // Inside a hunk's new text; advance next_hunk_idx_ to first hunk ending > raw_new_pos_.
    while (next_hunk_idx_ < hunks_.size() && hunks_[next_hunk_idx_].new_end <= raw_new_pos_) {
      ++next_hunk_idx_;
    }
  }

  if (raw_new_pos_ >= text_length_) {
    raw_mode_ = RawMode::AtEof;
  }
}

std::optional<uint32_t> TokenStream::unmap_after_hunk(uint32_t new_pos,
                                                      size_t& out_hunk_after) const noexcept {
  if (hunks_.empty()) {
    out_hunk_after = 0;
    return new_pos;
  }

  // Binary search: find first hunk with new_end > new_pos.
  auto it = std::upper_bound(
      hunks_.begin(), hunks_.end(), new_pos,
      [](uint32_t pos, const OldHunkInfo& h) { return pos < h.new_end; });

  const size_t idx = static_cast<size_t>(it - hunks_.begin());
  if (idx < hunks_.size() && new_pos >= hunks_[idx].new_start) {
    // Strictly inside [new_start, new_end) of hunks_[idx].
    out_hunk_after = idx;
    return std::nullopt;
  }

  // Strictly before hunks_[idx] (or past all hunks if idx == hunks_.size()).
  out_hunk_after = idx;
  const int64_t delta = (idx == 0) ? 0 : hunks_[idx - 1].delta_after;
  const int64_t old_pos = static_cast<int64_t>(new_pos) - delta;
  if (old_pos < 0) return std::nullopt;
  return static_cast<uint32_t>(old_pos);
}

bool TokenStream::is_clean_for_hunks_from(uint32_t old_start, uint32_t width,
                                          uint32_t reach_ahead,
                                          size_t start_hunk_idx) const noexcept {
  if (start_hunk_idx >= hunks_.size()) return true;
  const uint64_t reach_end =
      static_cast<uint64_t>(old_start) + static_cast<uint64_t>(width) + reach_ahead;
  // Next hunk is hunks_[start_hunk_idx]. Note: since old_start <= hunks_[start_hunk_idx].old_start
  // (when called at/before hunk), checking reach_end <= old_start suffices; check old_start < old_start too.
  const OldHunkInfo& h = hunks_[start_hunk_idx];
  return old_start < h.old_start && reach_end <= h.old_start;
}

void TokenStream::advance_sync_cursor_past_missing() {
  while (sync_cursor_.valid()) {
    const uast::GreenToken* tok = sync_cursor_.as_token();
    if (tok && !tok->is_missing()) {
      sync_cursor_valid_ = true;
      return;
    }
    if (!sync_cursor_.next_leaf()) break;
  }
  sync_cursor_valid_ = false;
}

bool TokenStream::sync_cursor_to_old_offset(uint32_t old_pos) {
  if (!opts_.old_tree || old_pos > old_length_) {
    sync_cursor_valid_ = false;
    return false;
  }

  if (sync_cursor_valid_ && sync_cursor_.valid()) {
    const auto* cur_tok = sync_cursor_.as_token();
    if (cur_tok && !cur_tok->is_missing() && sync_cursor_.range().start == old_pos) {
      return true;
    }
  }

  auto hit = uast::find_token_at_offset(opts_.old_tree, old_pos);
  if (!hit) {
    sync_cursor_valid_ = false;
    return false;
  }

  if (!sync_cursor_.valid() || sync_cursor_.root_node() != opts_.old_tree) {
    sync_cursor_ = uast::Cursor(opts_.old_tree);
  }
  if (!sync_cursor_.goto_token(hit->token_index)) {
    sync_cursor_valid_ = false;
    return false;
  }
  sync_cursor_valid_ = true;
  advance_sync_cursor_past_missing();
  if (!sync_cursor_valid_ || sync_cursor_.range().start != old_pos) {
    sync_cursor_valid_ = false;
    return false;
  }
  return true;
}

void TokenStream::maybe_leave_sync_for_next_hunk() {
  if (raw_mode_ != RawMode::Synced) return;
  if (next_hunk_idx_ >= hunks_.size()) {
    if ((!sync_cursor_valid_ || !sync_cursor_.valid()) &&
        (raw_old_pos_ >= old_length_ || raw_new_pos_ >= text_length_)) {
      raw_mode_ = RawMode::AtEof;
    }
    return;
  }

  const OldHunkInfo& h = hunks_[next_hunk_idx_];
  if (raw_old_pos_ >= h.old_start) {
    raw_mode_ = RawMode::Lexing;
    dirty_old_end_ = h.old_end;
    return;
  }

  if (!sync_cursor_valid_ || !sync_cursor_.valid()) {
    if (!sync_cursor_to_old_offset(raw_old_pos_)) {
      raw_mode_ = RawMode::Lexing;
      dirty_old_end_ = h.old_end;
      return;
    }
  }

  const uast::GreenToken* tok = sync_cursor_.as_token();
  if (!tok) {
    raw_mode_ = RawMode::Lexing;
    dirty_old_end_ = h.old_end;
    return;
  }

  const uint64_t reach_end =
      static_cast<uint64_t>(raw_old_pos_) + tok->width + tok->reach_ahead;
  if (reach_end > h.old_start) {
    raw_state_id_ = tok->lex_state_in;
    raw_mode_ = RawMode::Lexing;
    dirty_old_end_ = h.old_end;
  }
}

void TokenStream::try_enter_sync_at_raw_pos() {
  if (raw_mode_ != RawMode::Lexing || !opts_.old_tree) return;

  size_t hunk_after = 0;
  auto opt_old = unmap_after_hunk(raw_new_pos_, hunk_after);
  if (!opt_old) return;

  const uint32_t p_old = *opt_old;
  // Must have passed at least the current hunk(s) that triggered this lexing run.
  if (hunk_after <= next_hunk_idx_ && !hunks_.empty()) return;
  if (p_old < dirty_old_end_) return;

  if (p_old > old_length_) return;

  if (hunk_after < hunks_.size() && p_old >= hunks_[hunk_after].old_start) {
    return;
  }

  if (!sync_cursor_to_old_offset(p_old)) {
    if (p_old == old_length_ && hunk_after >= hunks_.size() &&
        raw_new_pos_ >= text_length_ && raw_state_id_ == 0) {
      raw_mode_ = RawMode::AtEof;
    }
    return;
  }

  // Inspect candidate token(s) starting at p_old (may have 0-width tokens before width > 0).
  const uast::GreenToken* tok = sync_cursor_.as_token();
  while (tok) {
    if (tok->lex_state_in == raw_state_id_) {
      if (is_clean_for_hunks_from(p_old, tok->width, tok->reach_ahead, hunk_after)) {
        raw_mode_ = RawMode::Synced;
        raw_old_pos_ = p_old;
        next_hunk_idx_ = hunk_after;
      }
      return;
    }
    if (tok->width > 0) return;
    if (!sync_cursor_.next_leaf()) break;
    advance_sync_cursor_past_missing();
    if (!sync_cursor_valid_ || sync_cursor_.range().start != p_old) break;
    tok = sync_cursor_.as_token();
  }
}

uint32_t TokenStream::hash_new_range(uint32_t offset, uint32_t width) const {
  if (width == 0 || !input_) return 2166136261u;
  uint32_t h = 2166136261u;
  uint32_t rem = width;
  uint32_t cur = offset;
  const lx_input* in = input_->input();
  while (rem > 0) {
    uint32_t chunk_len = 0;
    const uint8_t* ptr = in->chunk(in->ctx, cur, &chunk_len);
    if (!ptr || chunk_len == 0) break;
    const uint32_t take = std::min(rem, chunk_len);
    h = fnv1a_step(h, ptr, take);
    cur += take;
    rem -= take;
  }
  return h;
}

uast::GreenPtr TokenStream::make_or_intern_token(uast::Kind kind, uint32_t width,
                                                 uint32_t reach_ahead, uint32_t flags,
                                                 uint32_t text_hash, uint32_t state_in,
                                                 uint32_t state_out) {
  if (opts_.token_interner) {
    return opts_.token_interner->intern_or_create(
        kind, width, reach_ahead, flags, text_hash, state_in, state_out);
  }
  return uast::make_token(kind, width, reach_ahead, flags, text_hash, state_in, state_out);
}

const uast::GreenToken* TokenStream::find_old_token_exact(uint32_t old_pos,
                                                          uint32_t& out_tok_idx) {
  if (!opts_.old_tree || old_pos > old_length_) return nullptr;

  if (reid_cursor_valid_ && reid_cursor_.valid()) {
    const uint32_t cur_start = reid_cursor_.range().start;
    if (cur_start == old_pos) {
      const auto* tok = reid_cursor_.as_token();
      if (tok && !tok->is_missing()) {
        out_tok_idx = reid_cursor_.token_offset();
        return tok;
      }
    } else if (cur_start < old_pos && old_pos - cur_start <= 256) {
      while (reid_cursor_.next_leaf()) {
        const auto* tok = reid_cursor_.as_token();
        if (!tok || tok->is_missing()) continue;
        const uint32_t s = reid_cursor_.range().start;
        if (s == old_pos) {
          out_tok_idx = reid_cursor_.token_offset();
          return tok;
        }
        if (s > old_pos) break;
      }
    }
  }

  auto hit = uast::find_token_at_offset(opts_.old_tree, old_pos);
  if (!hit) {
    reid_cursor_valid_ = false;
    return nullptr;
  }
  if (!reid_cursor_.valid() || reid_cursor_.root_node() != opts_.old_tree) {
    reid_cursor_ = uast::Cursor(opts_.old_tree);
  }
  if (!reid_cursor_.goto_token(hit->token_index)) {
    reid_cursor_valid_ = false;
    return nullptr;
  }
  reid_cursor_valid_ = true;
  while (reid_cursor_.valid()) {
    const auto* tok = reid_cursor_.as_token();
    if (tok && !tok->is_missing()) {
      if (reid_cursor_.range().start == old_pos) {
        out_tok_idx = reid_cursor_.token_offset();
        return tok;
      }
      break;
    }
    if (!reid_cursor_.next_leaf()) break;
  }
  reid_cursor_valid_ = false;
  return nullptr;
}

bool TokenStream::try_read_synced(LexToken& out) {
  maybe_leave_sync_for_next_hunk();
  if (raw_mode_ != RawMode::Synced) return false;

  if (!sync_cursor_valid_ || !sync_cursor_.valid()) {
    if (!sync_cursor_to_old_offset(raw_old_pos_)) {
      raw_mode_ = RawMode::Lexing;
      return false;
    }
  }

  const uast::GreenToken* tok = sync_cursor_.as_token();
  if (!tok || tok->is_missing()) {
    advance_sync_cursor_past_missing();
    tok = sync_cursor_valid_ ? sync_cursor_.as_token() : nullptr;
  }
  if (!tok) {
    raw_mode_ = RawMode::Lexing;
    return false;
  }

  out.green = uast::GreenPtr(tok);
  out.symbol = uast::kind_symbol(tok->kind);
  out.channel = tok->is_hidden() ? LX_CH_HIDDEN : LX_CH_DEFAULT;
  out.new_offset = raw_new_pos_;
  out.old_offset = raw_old_pos_;
  out.width = tok->width;
  out.reach_ahead = tok->reach_ahead;
  out.text_hash = tok->text_hash;
  out.lex_state_in = tok->lex_state_in;
  out.lex_state_out = tok->lex_state_out;
  out.from_old = true;
  out.is_error = (out.symbol == LX_SYMBOL_ERROR) || tok->is_error();

  raw_new_pos_ += tok->width;
  raw_old_pos_ += tok->width;
  raw_state_id_ = tok->lex_state_out;
  ++stats_.synced_tokens;

  if (sync_cursor_.next_leaf()) {
    advance_sync_cursor_past_missing();
  } else {
    sync_cursor_valid_ = false;
  }

  // Check if we reached old EOF (or next hunk).
  if (!sync_cursor_valid_ && raw_old_pos_ >= old_length_) {
    if (next_hunk_idx_ < hunks_.size()) {
      // Next hunk may be an insertion at old EOF.
      maybe_leave_sync_for_next_hunk();
    } else {
      raw_mode_ = RawMode::AtEof;
    }
  } else {
    maybe_leave_sync_for_next_hunk();
  }

  return true;
}

bool TokenStream::lex_one(LexToken& out) {
  if (raw_mode_ == RawMode::AtEof) return false;
  if (!opts_.lang || !opts_.lang->lex || !input_) {
    raw_mode_ = RawMode::AtEof;
    return false;
  }

  const LexState cur_ls = state_interner_->resolve_state(raw_state_id_);
  lx_lex_request req{};
  req.input = input_->input();
  req.offset = raw_new_pos_;
  req.mode = state_interner_->top_mode(cur_ls.mode_stack_id);
  req.ext = cur_ls.ext_id;

  lx_lex_result res{};
  const lx_status st = opts_.lang->lex(&req, &res);
  ++stats_.lex_calls;

  if (st != LX_OK) {
    last_status_ = st;
    raw_mode_ = RawMode::AtEof;
    return false;
  }

  if (res.symbol == LX_SYMBOL_EOF) {
    raw_mode_ = RawMode::AtEof;
    return false;
  }

  uint32_t next_state_id =
      state_interner_->apply(raw_state_id_, res.cmd, res.cmd_arg, res.ext_next);

  // Guard against stuck zero-width non-EOF tokens without state progress.
  if (res.width == 0 && next_state_id == raw_state_id_) {
    if (raw_new_pos_ >= text_length_) {
      raw_mode_ = RawMode::AtEof;
      return false;
    }
    res.symbol = LX_SYMBOL_ERROR;
    res.width = 1;
    res.channel = LX_CH_DEFAULT;
  }

  // Clamp width defensively if a broken lexer overruns input length.
  if (raw_new_pos_ + res.width > text_length_) {
    res.width = text_length_ - raw_new_pos_;
    if (res.width == 0 && next_state_id == raw_state_id_) {
      raw_mode_ = RawMode::AtEof;
      return false;
    }
  }

  uint8_t channel = res.channel;
  if (lang_info_ && lang_info_->symbols && res.symbol < lang_info_->symbol_count) {
    if ((lang_info_->symbols[res.symbol].flags & LX_SYM_HIDDEN) != 0) {
      channel = LX_CH_HIDDEN;
    }
  }

  const uast::Kind kind = uast::make_kind(opts_.lang_idx, res.symbol);
  uint32_t flags = uast::FLAG_LEAF;
  if (channel == LX_CH_HIDDEN) flags |= uast::FLAG_HIDDEN;
  if (res.symbol == LX_SYMBOL_ERROR) flags |= uast::FLAG_ERROR;

  const uint32_t text_hash = hash_new_range(raw_new_pos_, res.width);

  // §6.2 step 4: Token reidentification against old_tree.
  uast::GreenPtr green;
  bool from_old = false;
  uint32_t old_off = 0;

  if (opts_.old_tree) {
    size_t unused_hunk = 0;
    if (auto opt_old = unmap_after_hunk(raw_new_pos_, unused_hunk)) {
      const uint32_t p_old = *opt_old;
      uint32_t dummy_idx = 0;
      const uast::GreenToken* old_tok = find_old_token_exact(p_old, dummy_idx);
      while (old_tok) {
        if (old_tok->kind == kind && old_tok->width == res.width &&
            old_tok->reach_ahead == res.reach_ahead && old_tok->flags == flags &&
            old_tok->text_hash == text_hash && old_tok->lex_state_in == raw_state_id_ &&
            old_tok->lex_state_out == next_state_id) {
          green = uast::GreenPtr(old_tok);
          from_old = true;
          old_off = p_old;
          ++stats_.reidentified_tokens;
          break;
        }
        if (old_tok->width > 0) break;
        if (!reid_cursor_.next_leaf()) break;
        old_tok = reid_cursor_.as_token();
        while (old_tok && old_tok->is_missing()) {
          if (!reid_cursor_.next_leaf()) {
            old_tok = nullptr;
            break;
          }
          old_tok = reid_cursor_.as_token();
        }
        if (!old_tok || reid_cursor_.range().start != p_old) break;
      }
    }
  }

  if (!green) {
    green = make_or_intern_token(kind, res.width, res.reach_ahead, flags, text_hash,
                                 raw_state_id_, next_state_id);
    ++stats_.new_tokens;
  }

  out.green = std::move(green);
  out.symbol = res.symbol;
  out.channel = channel;
  out.new_offset = raw_new_pos_;
  out.old_offset = old_off;
  out.width = res.width;
  out.reach_ahead = res.reach_ahead;
  out.text_hash = text_hash;
  out.lex_state_in = raw_state_id_;
  out.lex_state_out = next_state_id;
  out.from_old = from_old;
  out.is_error = (res.symbol == LX_SYMBOL_ERROR);

  raw_new_pos_ += res.width;
  raw_state_id_ = next_state_id;

  if (raw_new_pos_ >= text_length_) {
    // Allow trailing 0-width tokens only if state still transitions; otherwise check sync/EOF.
    // Note: next lex() call at text_length_ may emit trailing DEDENTs before returning EOF.
    try_enter_sync_at_raw_pos();
  } else {
    try_enter_sync_at_raw_pos();
  }

  return true;
}

bool TokenStream::read_raw_token(LexToken& out, bool& out_from_synced) {
  if (raw_mode_ == RawMode::AtEof) return false;
  if (raw_mode_ == RawMode::Synced) {
    if (try_read_synced(out)) {
      out_from_synced = true;
      return true;
    }
    if (raw_mode_ == RawMode::AtEof) return false;
  }
  out_from_synced = false;
  return lex_one(out);
}

void TokenStream::advance_one_default() {
  BufferedDefault slot;
  uint32_t zero_width_guard = 0;

  while (true) {
    // Capture synced status BEFORE reading the token that lands in this slot.
    if (raw_mode_ == RawMode::Synced) {
      maybe_leave_sync_for_next_hunk();
    }
    const bool cur_synced = (raw_mode_ == RawMode::Synced);

    LexToken tok;
    bool from_synced = false;
    if (!read_raw_token(tok, from_synced)) {
      slot.token = LexToken{};
      slot.token.symbol = LX_SYMBOL_EOF;
      slot.token.channel = LX_CH_DEFAULT;
      slot.token.new_offset = raw_new_pos_;
      slot.token.old_offset = (raw_mode_ == RawMode::Synced) ? raw_old_pos_ : 0;
      slot.token.lex_state_in = raw_state_id_;
      slot.token.lex_state_out = raw_state_id_;
      slot.token.from_old = false;
      slot.from_synced = cur_synced;
      default_buf_.push_back(std::move(slot));
      return;
    }

    if (tok.width == 0) {
      if (++zero_width_guard > kMaxZeroWidthSteps) {
        tok.symbol = LX_SYMBOL_EOF;
        slot.token = std::move(tok);
        slot.from_synced = false;
        raw_mode_ = RawMode::AtEof;
        default_buf_.push_back(std::move(slot));
        return;
      }
    } else {
      zero_width_guard = 0;
    }

    if (tok.is_hidden()) {
      slot.leading_hidden.push_back(std::move(tok));
      continue;
    }

    slot.token = std::move(tok);
    slot.from_synced = from_synced;
    default_buf_.push_back(std::move(slot));
    return;
  }
}

void TokenStream::ensure_default(uint32_t k) {
  while (default_buf_.size() <= k) {
    if (!default_buf_.empty() && default_buf_.back().token.is_eof()) {
      return;
    }
    advance_one_default();
  }
}

const LexToken& TokenStream::peek(uint32_t k) {
  ensure_default(k);
  if (k < default_buf_.size()) {
    return default_buf_[k].token;
  }
  return default_buf_.back().token;
}

LexToken TokenStream::next() {
  ensure_default(0);
  LexToken tok = default_buf_.front().token;
  if (tok.is_eof()) {
    return tok;
  }
  push_lookback(tok);
  default_buf_.pop_front();
  return tok;
}

std::vector<LexToken> TokenStream::take_hidden() {
  ensure_default(0);
  std::vector<LexToken> out;
  out.swap(default_buf_.front().leading_hidden);
  return out;
}

bool TokenStream::has_pending_hidden() {
  ensure_default(0);
  return !default_buf_.front().leading_hidden.empty();
}

bool TokenStream::from_old() {
  return peek(0).from_old;
}

uint32_t TokenStream::old_offset() {
  return peek(0).old_offset;
}

bool TokenStream::synced() {
  ensure_default(0);
  return default_buf_.front().from_synced;
}

uint32_t TokenStream::new_offset() {
  return peek(0).new_offset;
}

uint32_t TokenStream::state_id() {
  return peek(0).lex_state_in;
}

LexState TokenStream::state() {
  return state_interner_->resolve_state(state_id());
}

void TokenStream::jump(uint32_t new_offset, LexState state) {
  jump(new_offset, state_interner_->intern_state(state));
}

void TokenStream::jump(uint32_t new_offset, uint32_t lex_state_id) {
  default_buf_.clear();
  lookback_.clear();

  raw_new_pos_ = std::min(new_offset, text_length_);
  raw_state_id_ = lex_state_id;
  dirty_old_end_ = 0;

  if (!opts_.old_tree) {
    raw_mode_ = RawMode::Lexing;
    return;
  }

  size_t hunk_after = 0;
  auto opt_old = unmap_after_hunk(raw_new_pos_, hunk_after);
  if (opt_old) {
    next_hunk_idx_ = hunk_after;
    const uint32_t p_old = *opt_old;
    if (p_old > old_length_) {
      if (hunk_after >= hunks_.size() && raw_new_pos_ >= text_length_ && raw_state_id_ == 0) {
        raw_mode_ = RawMode::AtEof;
      } else {
        raw_mode_ = RawMode::Lexing;
        if (hunk_after < hunks_.size()) dirty_old_end_ = hunks_[hunk_after].old_end;
      }
      return;
    }

    if ((hunk_after >= hunks_.size() || p_old < hunks_[hunk_after].old_start) &&
        sync_cursor_to_old_offset(p_old)) {
      const uast::GreenToken* tok = sync_cursor_.as_token();
      while (tok) {
        if (tok->lex_state_in == raw_state_id_) {
          if (is_clean_for_hunks_from(p_old, tok->width, tok->reach_ahead, hunk_after)) {
            raw_mode_ = RawMode::Synced;
            raw_old_pos_ = p_old;
            return;
          }
          break;
        }
        if (tok->width > 0) break;
        if (!sync_cursor_.next_leaf()) break;
        advance_sync_cursor_past_missing();
        if (!sync_cursor_valid_ || sync_cursor_.range().start != p_old) break;
        tok = sync_cursor_.as_token();
      }
    }
    raw_mode_ = RawMode::Lexing;
    if (hunk_after < hunks_.size()) {
      dirty_old_end_ = hunks_[hunk_after].old_end;
    }
    return;
  }

  // Fallback if jumped inside a hunk's new range.
  next_hunk_idx_ = hunk_after;
  if (hunk_after < hunks_.size()) {
    dirty_old_end_ = hunks_[hunk_after].old_end;
  }
  raw_mode_ = RawMode::Lexing;
}

void TokenStream::push_lookback(const LexToken& tok) {
  if (opts_.lookback_capacity == 0) return;
  if (lookback_.size() >= opts_.lookback_capacity) {
    lookback_.pop_front();
  }
  lookback_.push_back(tok);
}

const LexToken* TokenStream::lookback(int32_t i) const noexcept {
  if (i >= 0) return nullptr;
  const uint64_t back = static_cast<uint64_t>(-static_cast<int64_t>(i));
  if (back == 0 || back > lookback_.size()) return nullptr;
  return &lookback_[lookback_.size() - static_cast<size_t>(back)];
}

lx_symbol TokenStream::la_symbol(int32_t i) {
  if (i > 0) {
    return peek(static_cast<uint32_t>(i - 1)).symbol;
  }
  if (i < 0) {
    const LexToken* tok = lookback(i);
    return tok ? tok->symbol : LX_SYMBOL_INVALID;
  }
  return LX_SYMBOL_INVALID;
}

std::string TokenStream::token_text(const LexToken& tok) const {
  if (tok.width == 0) return {};
  if (opts_.new_view) {
    return opts_.new_view->slice_text(tok.new_offset, tok.new_end());
  }
  if (opts_.new_rope) {
    return opts_.new_rope->slice_text(tok.new_offset, tok.new_end());
  }
  return {};
}

bool TokenStream::is_line_start(const LexToken& tok) const {
  if (tok.new_offset == 0) return true;
  // Walk backwards across chunks or check bytes from last '\n' to tok.new_offset.
  // Fast path via Rope::byte_to_point / line_start_byte when using Rope directly.
  if (opts_.new_rope) {
    const Point pt = opts_.new_rope->byte_to_point(tok.new_offset);
    const uint32_t line_start = tok.new_offset - pt.column;
    for (uint32_t p = line_start; p < tok.new_offset; ++p) {
      const uint8_t b = opts_.new_rope->byte_at(p);
      if (b != ' ' && b != '\t') return false;
    }
    return true;
  }
  if (opts_.new_view) {
    uint32_t p = tok.new_offset;
    while (p > 0) {
      --p;
      uint32_t len = 0;
      const uint8_t* ptr = input_->input()->chunk(input_->input()->ctx, p, &len);
      if (!ptr || len == 0) break;
      const uint8_t b = ptr[0];
      if (b == '\n') return true;
      if (b != ' ' && b != '\t') return false;
    }
    return true;
  }
  return false;
}

std::vector<LexToken> TokenStream::collect_all_tokens() {
  std::vector<LexToken> out;
  while (true) {
    auto hidden = take_hidden();
    for (auto& h : hidden) {
      out.push_back(std::move(h));
    }
    if (peek(0).is_eof()) break;
    out.push_back(next());
  }
  return out;
}

}  // namespace lx
