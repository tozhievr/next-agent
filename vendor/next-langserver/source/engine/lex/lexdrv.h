#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "lx/lx_constants.h"
#include "lx/lx_language.h"
#include "state_interner.h"

#if __has_include("rope/rope.h")
#include "rope/rope.h"
#else
#include "rope.h"
#endif

#if __has_include("uast/uast.h")
#include "uast/uast.h"
#else
#include "uast.h"
#endif

namespace lx {

/// Single token produced or replayed by TokenStream (§6.2, App B.2/B.3).
struct LexToken {
  uast::GreenPtr green;               // GreenToken (null only for synthetic EOF)
  lx_symbol symbol = LX_SYMBOL_EOF;
  uint8_t channel = LX_CH_DEFAULT;
  uint32_t new_offset = 0;            // Byte offset in new text
  uint32_t old_offset = 0;            // Byte offset in old_tree (valid iff from_old == true)
  uint32_t width = 0;                 // Byte width (> 0 for normal tokens; 0 for EOF or synthetic 0-width)
  uint32_t reach_ahead = 0;           // Bytes read past token end
  uint32_t text_hash = 0;             // FNV-1a 32-bit hash of token bytes
  uint32_t lex_state_in = 0;          // Interned LexState id before this token
  uint32_t lex_state_out = 0;         // Interned LexState id after this token
  bool from_old = false;              // True if fetched from old_tree OR reidentified
  bool is_error = false;              // True if symbol == LX_SYMBOL_ERROR

  bool is_eof() const noexcept { return symbol == LX_SYMBOL_EOF; }
  bool is_hidden() const noexcept { return channel == LX_CH_HIDDEN; }
  uint32_t new_end() const noexcept { return new_offset + width; }
  const uast::GreenToken* green_token() const noexcept {
    return green ? green->as_token() : nullptr;
  }
};

struct TokenStreamOptions {
  const lx_language_v1* lang = nullptr;
  uint16_t lang_idx = 0;
  StateInterner* state_interner = nullptr;
  uast::TokenInterner* token_interner = nullptr; // Optional; if null uses uast::make_token
  const Rope* new_rope = nullptr;                // Required unless new_view is provided
  const RopeView* new_view = nullptr;            // Optional embedded language view (§4.5)
  const uast::GreenNode* old_tree = nullptr;     // Null for full lexing
  const ChangeSet* changes = nullptr;            // Null/empty for full lexing
  uint32_t start_offset = 0;
  LexState initial_state = {};
  uint32_t lookback_capacity = 64;
};

/// Incremental token stream with reach-based start, synchronization,
/// token reidentification, skip->HIDDEN trivia buffering, and jump (§6.2, B.2).
class TokenStream {
 public:
  struct Stats {
    uint64_t lex_calls = 0;
    uint64_t synced_tokens = 0;
    uint64_t reidentified_tokens = 0;
    uint64_t new_tokens = 0;
  };

  explicit TokenStream(TokenStreamOptions opts);
  ~TokenStream();

  TokenStream(const TokenStream&) = delete;
  TokenStream& operator=(const TokenStream&) = delete;

  // --- Parser default-channel interface ---
  // Lookahead k default-channel tokens ahead (k = 0 is current token, App B.3).
  // Returned reference stays valid across subsequent peek(larger_k), until next()/jump().
  const LexToken& peek(uint32_t k = 0);
  const LexToken& peek_token() { return peek(0); }

  // Consumes and returns the current default-channel token (returns EOF at end).
  LexToken next();

  // Drains HIDDEN tokens immediately preceding peek(0) (including trailing trivia before EOF).
  std::vector<LexToken> take_hidden();
  bool has_pending_hidden();

  // --- Incremental reuse & synchronization queries (App B.3 / B.4) ---
  bool from_old();
  uint32_t old_offset();
  bool synced();
  uint32_t new_offset();
  uint32_t state_id();
  LexState state();

  // Jumps stream forward after reusing a subtree or Seq prefix/suffix.
  // Caller should flush take_hidden() before enter_rule()/attach() if needed.
  void jump(uint32_t new_offset, uint32_t lex_state_id);
  void jump(uint32_t new_offset, LexState state);

  // --- Predicate / lookback support (lx_pred_view) ---
  // i <= -1: recently consumed default-channel tokens (-1 = most recent).
  const LexToken* lookback(int32_t i) const noexcept;
  // i >= 1 -> peek(i - 1).symbol; i <= -1 -> lookback(i)->symbol.
  lx_symbol la_symbol(int32_t i);
  std::string token_text(const LexToken& tok) const;
  bool is_line_start(const LexToken& tok) const;

  // --- Full raw iteration (DEFAULT + HIDDEN in byte order until EOF) ---
  std::vector<LexToken> collect_all_tokens();

  const Stats& stats() const noexcept { return stats_; }
  lx_status last_status() const noexcept { return last_status_; }
  uint32_t text_length() const noexcept { return text_length_; }

 private:
  enum class RawMode : uint8_t {
    Lexing = 0,
    Synced = 1,
    AtEof = 2,
  };

  struct BufferedDefault {
    LexToken token;
    std::vector<LexToken> leading_hidden;
    bool from_synced = false;
  };

  struct OldHunkInfo {
    uint32_t old_start = 0;
    uint32_t old_end = 0;
    uint32_t new_start = 0;
    uint32_t new_end = 0;
    int64_t delta_after = 0; // sum of (new_len - old_len) for hunks [0..i]
  };

  void init_hunks();
  void init_start_mode();
  void ensure_default(uint32_t k);
  void advance_one_default();
  bool read_raw_token(LexToken& out, bool& out_from_synced);
  bool try_read_synced(LexToken& out);
  bool lex_one(LexToken& out);
  void try_enter_sync_at_raw_pos();
  void maybe_leave_sync_for_next_hunk();
  std::optional<uint32_t> unmap_after_hunk(uint32_t new_pos, size_t& out_hunk_after) const noexcept;
  bool is_clean_for_hunks_from(uint32_t old_start, uint32_t width, uint32_t reach_ahead,
                               size_t start_hunk_idx) const noexcept;
  const uast::GreenToken* find_old_token_exact(uint32_t old_pos, uint32_t& out_tok_idx);
  bool sync_cursor_to_old_offset(uint32_t old_pos);
  void advance_sync_cursor_past_missing();
  uint32_t hash_new_range(uint32_t offset, uint32_t width) const;
  uast::GreenPtr make_or_intern_token(uast::Kind kind, uint32_t width, uint32_t reach_ahead,
                                      uint32_t flags, uint32_t text_hash,
                                      uint32_t state_in, uint32_t state_out);
  void push_lookback(const LexToken& tok);

  TokenStreamOptions opts_;
  const lx_language_info* lang_info_ = nullptr;
  std::unique_ptr<StateInterner> owned_state_interner_;
  StateInterner* state_interner_ = nullptr;

  std::unique_ptr<RopeInput> input_;
  uint32_t text_length_ = 0;
  uint32_t old_length_ = 0;

  std::vector<OldHunkInfo> hunks_;

  // Raw cursor state
  RawMode raw_mode_ = RawMode::Lexing;
  uint32_t raw_new_pos_ = 0;
  uint32_t raw_old_pos_ = 0;
  uint32_t raw_state_id_ = 0;
  size_t next_hunk_idx_ = 0;     // Next hunk not yet passed in old coords
  uint32_t dirty_old_end_ = 0;   // Cannot sync at p_old < dirty_old_end_ while lexing

  uast::Cursor sync_cursor_;
  bool sync_cursor_valid_ = false;
  uast::Cursor reid_cursor_;
  bool reid_cursor_valid_ = false;

  std::deque<BufferedDefault> default_buf_;
  std::deque<LexToken> lookback_;

  Stats stats_{};
  lx_status last_status_ = LX_OK;
};

}  // namespace lx
