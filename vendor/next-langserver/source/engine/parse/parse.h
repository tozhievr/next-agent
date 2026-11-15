#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/atn/atn.h"
#include "engine/lex/lexdrv.h"
#include "engine/rope/rope.h"
#include "engine/uast/uast.h"
#include "lx/lx_constants.h"
#include "lx/lx_language.h"

namespace lx::parse {

// ============================================================================
// Diagnostics (§6.6, §10.4)
// ============================================================================

enum class DiagnosticCode : uint16_t {
  UnexpectedToken = 0, // LX_E_UNEXPECTED_TOKEN
  MissingToken = 1,    // LX_E_MISSING_TOKEN
  LexError = 2,        // LX_E_LEX_ERROR
  MaxErrors = 3,       // LX_E_MAX_ERRORS
  MaxDepth = 4,        // LX_E_MAX_DEPTH
  MaxNodes = 5,        // LX_E_MAX_NODES
  MaxFileBytes = 6,    // LX_E_MAX_FILE_BYTES
  MaxParseMs = 7,      // LX_E_MAX_PARSE_MS
  Internal = 8,        // LX_E_INTERNAL
};

[[nodiscard]] constexpr std::string_view diagnostic_code_str(
    DiagnosticCode code) noexcept {
  switch (code) {
    case DiagnosticCode::UnexpectedToken: return "LX_E_UNEXPECTED_TOKEN";
    case DiagnosticCode::MissingToken: return "LX_E_MISSING_TOKEN";
    case DiagnosticCode::LexError: return "LX_E_LEX_ERROR";
    case DiagnosticCode::MaxErrors: return "LX_E_MAX_ERRORS";
    case DiagnosticCode::MaxDepth: return "LX_E_MAX_DEPTH";
    case DiagnosticCode::MaxNodes: return "LX_E_MAX_NODES";
    case DiagnosticCode::MaxFileBytes: return "LX_E_MAX_FILE_BYTES";
    case DiagnosticCode::MaxParseMs: return "LX_E_MAX_PARSE_MS";
    case DiagnosticCode::Internal: return "LX_E_INTERNAL";
  }
  return "LX_E_INTERNAL";
}

enum class DiagnosticSeverity : uint8_t {
  Error = 1,
  Warning = 2,
  Info = 3,
  Hint = 4,
};

struct Diagnostic {
  DiagnosticCode code = DiagnosticCode::UnexpectedToken;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  uast::Range range{}; // Byte range [start, end) in parsed text
  lx_symbol expected_symbol = LX_SYMBOL_INVALID;
  lx_symbol actual_symbol = LX_SYMBOL_INVALID;
  std::string message;

  [[nodiscard]] std::string_view code_str() const noexcept {
    return diagnostic_code_str(code);
  }

  bool operator==(const Diagnostic&) const noexcept = default;
};

// Extracts stable diagnostics from Error / Missing / Truncated nodes in a tree (§6.6).
// Skips clean subtrees without FLAG_HAS_ERROR in O(1).
[[nodiscard]] std::vector<Diagnostic> extract_diagnostics(
    const uast::GreenNode* root,
    const lx_language_info* info = nullptr);

// ============================================================================
// Limits, Stats, Environment, and Snapshot View (§6.3, §10.4, §12.2)
// ============================================================================

struct ParseLimits {
  uint32_t max_file_bytes = 16u * 1024u * 1024u; // 16 MiB
  uint32_t max_tree_depth = 512;
  uint32_t max_nodes = 2'000'000;
  uint32_t max_parse_ms = 0; // 0 = unlimited
  uint32_t max_errors = 100;
  uint32_t max_error_tokens = 32;  // Max tokens skipped in one panic-mode Error node
  uint32_t error_heal_tokens = 8;  // K tokens for near_dirty_error (§6.3)
  double full_reparse_ratio = 0.5; // cs.change_ratio > threshold -> full parse (App B.1)
};

struct ParseStats {
  uint64_t tokens_lexed = 0;
  uint64_t tokens_reidentified = 0;
  uint64_t tokens_synced = 0;
  uint64_t nodes_reused = 0;
  uint64_t nodes_created = 0;
  uint64_t seq_fast_forwards = 0;
  uint64_t seq_suffix_adoptions = 0;
  uint64_t ll1_decisions = 0;
  uint64_t llk_decisions = 0;
  uint64_t sll_dfa_hits = 0;
  uint64_t sll_dfa_misses = 0;
  uint64_t full_ll_decisions = 0;
  uint64_t error_recoveries = 0;
  uint64_t elapsed_us = 0;
};

struct ParseEnv {
  uint64_t(LX_CALL* env_get)(void* ctx, uint32_t dep_id) = nullptr;
  void* ctx = nullptr;

  [[nodiscard]] uint64_t get(uint32_t dep_id) const noexcept {
    return env_get ? env_get(ctx, dep_id) : 0ULL;
  }
};

struct OldSnapshotView {
  const uast::GreenNode* root = nullptr;
  uint64_t grammar_hash = 0;
  std::span<const std::pair<uint32_t, uint64_t>> env_deps{};
};

struct ParseResult {
  lx_status status = LX_OK;
  uast::GreenPtr root;
  std::vector<Diagnostic> diagnostics;
  std::vector<uast::Range> changed_ranges;
  bool used_incremental = false;
  bool truncated = false;
  std::vector<std::pair<uint32_t, uint64_t>> env_deps_snapshot;
  ParseStats stats{};
};

// ============================================================================
// Thread-Safe SLL DFA Cache (§6.5, §10.1, §10.3)
// ============================================================================

/// Per-module thread-safe DFA cache for ALL(*) prediction.
/// Lock-free reads on cached transitions; mutex-protected expansion on misses.
class DfaCache {
 public:
  explicit DfaCache(const atn::Atn* atn = nullptr,
                    uint32_t max_states_per_decision = 4096);
  ~DfaCache();

  DfaCache(const DfaCache&) = delete;
  DfaCache& operator=(const DfaCache&) = delete;

  void reset(const atn::Atn* atn = nullptr,
             uint32_t max_states_per_decision = 4096);

  [[nodiscard]] size_t decision_count() const noexcept;
  [[nodiscard]] uint64_t total_states() const noexcept;

  struct Impl;
  Impl* impl() noexcept { return impl_.get(); }
  const Impl* impl() const noexcept { return impl_.get(); }

 private:
  std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Context Hash Helper (§5.6, R3)
// ============================================================================

/// Computes the deterministic 32-bit hash of the outermost `depth` ATN state IDs
/// from the end of `state_stack` (returns 0 if depth == 0).
[[nodiscard]] uint32_t compute_ctx_hash(
    std::span<const uint32_t> state_stack,
    uint32_t depth) noexcept;

// ============================================================================
// Reuse Oracle (R1-R4 + near_dirty_error + Seq splicing, §6.3, §6.4, App B.3/B.4)
// ============================================================================

class ReuseOracle {
 public:
  struct RuleCandidate {
    const uast::GreenNode* node = nullptr;
    uint32_t old_start = 0;
    uint32_t last_lex_state_out = 0;
  };

  ReuseOracle() = default;
  ReuseOracle(const uast::GreenNode* old_root,
              std::span<const uast::DirtySpan> dirty_spans,
              uint32_t error_heal_tokens = 8,
              bool env_dirty = false);

  void reset(const uast::GreenNode* old_root,
             std::span<const uast::DirtySpan> dirty_spans,
             uint32_t error_heal_tokens = 8,
             bool env_dirty = false);

  [[nodiscard]] bool empty() const noexcept { return old_root_ == nullptr; }
  [[nodiscard]] const uast::GreenNode* old_root() const noexcept { return old_root_; }
  [[nodiscard]] std::span<const uast::DirtySpan> dirty_spans() const noexcept {
    return dirty_spans_;
  }

  /// Checks R1-R4 + near_dirty_error at `old_offset` for rule `expected_kind` with `expected_prec`.
  /// `active_ctx_states` holds the caller ATN state IDs on the rule call stack.
  [[nodiscard]] std::optional<RuleCandidate> find_reusable_rule(
      uint32_t old_offset,
      uast::Kind expected_kind,
      uint32_t expected_prec,
      std::span<const uint32_t> active_ctx_states) const;

  /// Same as find_reusable_rule, matching any symbol kind in `accepted_kinds` (for #Label rules).
  [[nodiscard]] std::optional<RuleCandidate> find_reusable_rule_any(
      uint32_t old_offset,
      std::span<const uast::Kind> accepted_kinds,
      uint32_t expected_prec,
      std::span<const uint32_t> active_ctx_states) const;

  /// Checks whether an already-located node satisfies R2, R3, R4, and !near_dirty_error.
  [[nodiscard]] bool can_reuse(
      const uast::GreenNode* node,
      uint32_t old_start,
      uint32_t expected_prec,
      std::span<const uint32_t> active_ctx_states) const;

  /// Returns true iff `node` has FLAG_HAS_ERROR and a dirty span lies within K tokens of its bounds.
  [[nodiscard]] bool is_near_dirty_error(
      const uast::GreenNode* node,
      uint32_t old_start) const;

  /// Checks R3/R4 on a candidate Seq node for a LOOP_SPLICEABLE loop.
  [[nodiscard]] bool can_splice_seq(
      const uast::GreenNode* seq,
      std::span<const uint32_t> active_ctx_states) const;

  /// Finds a Seq child inside an old rule starting at `rule_old_start` (or at `seq_old_start`).
  [[nodiscard]] const uast::GreenNode* find_seq_starting_at(
      uint32_t seq_old_start,
      uast::Kind enclosing_rule_kind = 0) const;

  /// Computes longest clean prefix of `seq` starting at `seq_start_old` (App B.4 fast_forward).
  [[nodiscard]] uast::GreenPtr fast_forward_seq(
      const uast::GreenNode* seq,
      uint32_t seq_start_old,
      std::span<const uint32_t> active_ctx_states) const;

  /// Computes clean run starting at `from_old_byte` inside `seq` (App B.4 adopt_suffix).
  [[nodiscard]] uast::GreenPtr adopt_suffix_seq(
      const uast::GreenNode* seq,
      uint32_t seq_start_old,
      uint32_t from_old_byte) const;

  [[nodiscard]] static uint32_t last_token_lex_state_out(
      const uast::GreenNode* node) noexcept;

 private:
  const uast::GreenNode* old_root_ = nullptr;
  std::vector<uast::DirtySpan> dirty_spans_;
  uint32_t error_heal_tokens_ = 8;
  bool env_dirty_ = false;
  mutable uast::Cursor cursor_;
};

// ============================================================================
// ParseOptions & ParseDriver (§6, App B.1)
// ============================================================================

struct ParseOptions {
  const lx_language_v1* lang = nullptr;
  const atn::Atn* atn = nullptr;
  uint16_t lang_idx = 0;
  uint16_t start_rule_override = atn::kIndexNoneU16; // Defaults to atn->start_rule()
  StateInterner* state_interner = nullptr;           // Optional; created per-driver if null
  uast::TokenInterner* token_interner = nullptr;     // Optional
  DfaCache* dfa_cache = nullptr;                     // Optional; created per-driver if null
  LexState initial_lex_state = {};
  ParseEnv env = {};
  ParseLimits limits = {};
  const std::atomic<bool>* cancel_flag = nullptr;
  uint64_t expected_grammar_hash = 0;
  bool compute_diff = true;
};

class ParseDriver {
 public:
  explicit ParseDriver(ParseOptions opts);
  ~ParseDriver();

  ParseDriver(const ParseDriver&) = delete;
  ParseDriver& operator=(const ParseDriver&) = delete;

  [[nodiscard]] const ParseOptions& options() const noexcept { return opts_; }

  [[nodiscard]] ParseResult parse_full(const Rope& rope);
  [[nodiscard]] ParseResult parse_full(const RopeView& view);

  [[nodiscard]] ParseResult reparse(const OldSnapshotView& old_snapshot,
                                    const ChangeSet& changes,
                                    const Rope& new_rope);
  [[nodiscard]] ParseResult reparse(const OldSnapshotView& old_snapshot,
                                    const ChangeSet& changes,
                                    const RopeView& new_view);

 private:
  ParseResult run_parse(const Rope* rope,
                        const RopeView* view,
                        const OldSnapshotView* old_snapshot,
                        const ChangeSet* changes);

  ParseOptions opts_;
  std::unique_ptr<StateInterner> owned_state_interner_;
  std::unique_ptr<DfaCache> owned_dfa_cache_;
};

// Convenience free functions matching §6 / App B.1.
[[nodiscard]] ParseResult parse_full(const ParseOptions& opts, const Rope& rope);
[[nodiscard]] ParseResult parse_full(const ParseOptions& opts, const RopeView& view);

[[nodiscard]] ParseResult reparse(const ParseOptions& opts,
                                  const OldSnapshotView& old_snapshot,
                                  const ChangeSet& changes,
                                  const Rope& new_rope);
[[nodiscard]] ParseResult reparse(const ParseOptions& opts,
                                  const OldSnapshotView& old_snapshot,
                                  const ChangeSet& changes,
                                  const RopeView& new_view);

} // namespace lx::parse
