#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "lx/lx_constants.h"
#include "lx/lx_language.h"

namespace lx::atn {

inline constexpr uint16_t kIndexNoneU16 = 0xFFFFu;
inline constexpr uint32_t kIndexNoneU32 = 0xFFFFFFFFu;
inline constexpr uint16_t kTblError = 0xFFFFu;
inline constexpr uint16_t kTblAmbig = 0xFFFEu;

enum class StateKind : uint8_t {
  Invalid = 0,
  Basic = 1,
  RuleStart = 2,
  RuleStop = 3,
  BlockStart = 4,
  BlockEnd = 5,
  PlusBlockStart = 6,
  PlusLoopBack = 7,
  StarLoopEntry = 8,
  StarBlockStart = 9,
  StarLoopBack = 10,
  LoopEnd = 11,
  TokensStart = 12,
};

enum StateFlags : uint8_t {
  kStateLoopSpliceable = 1u << 0,
  kStateNonGreedy = 1u << 1,
  kStateLeftRecEntry = 1u << 2,
};

enum class TransitionKind : uint8_t {
  Epsilon = 0,
  Atom = 1,
  Set = 2,
  NotSet = 3,
  Range = 4,
  Rule = 5,
  Pred = 6,
  Precedence = 7,
  Action = 8,
  Wildcard = 9,
};

enum TransitionFlags : uint8_t {
  kTransListAdd = 1u << 0,
  kTransPrecedenceArg = 1u << 1,
};

enum PredTransFlags : uint32_t {
  kTransPredNegated = 1u << 0,
};

enum RuleFlags : uint16_t {
  kRuleLeftRecursive = 1u << 0,
  kRuleListItem = 1u << 1,
  kRuleSupertype = 1u << 2,
  kRuleFlatten = 1u << 3,
  kRuleFragmentEntry = 1u << 4,
  kRuleStart = 1u << 5,
  kRuleLoopSpliceable = 1u << 6,
};

enum class DecisionKind : uint8_t {
  LL1 = 0,
  LLK = 1,
  ALL = 2,
};

enum DecisionFlags : uint8_t {
  kDecHasPred = 1u << 0,
  kDecNonGreedy = 1u << 1,
};

enum AltLabelFlags : uint16_t {
  kAltLeftRec = 1u << 0,
  kAltRightAssoc = 1u << 1,
};

enum PredFlags : uint32_t {
  kPredPure = 1u << 0,
  kPredHasDeps = 1u << 1,
  kPredPosSensitive = 1u << 2,
};

struct AtnInterval {
  lx_symbol lo = 0;
  lx_symbol hi = 0;
};

struct AtnSet {
  uint32_t interval_start = 0;
  uint32_t interval_count = 0;
};

struct AtnState {
  StateKind kind = StateKind::Invalid;
  uint8_t flags = 0;
  uint16_t rule_id = 0;
  uint16_t decision_id = kIndexNoneU16;
  uint16_t sync_set_id = kIndexNoneU16;
  uint32_t trans_start = 0;
  uint16_t trans_count = 0;
  uint16_t alt_index = kIndexNoneU16;
  uint32_t pair_state = kIndexNoneU32;
  uint32_t loop_end_state = kIndexNoneU32;
};

struct AtnTransition {
  uint32_t from = 0;
  uint32_t to = 0;
  TransitionKind kind = TransitionKind::Epsilon;
  uint8_t flags = 0;
  lx_field field_id = LX_FIELD_NONE;
  uint32_t arg0 = 0;
  uint32_t arg1 = 0;
};

struct AtnRule {
  uint32_t start_state = 0;
  uint32_t stop_state = 0;
  lx_symbol symbol_id = LX_SYMBOL_INVALID;
  uint16_t flags = 0;
  uint16_t max_prec = 0;
  uint16_t sync_set_id = kIndexNoneU16;
  uint32_t field_map_off = 0;
  uint32_t field_map_count = 0;
  uint32_t alt_label_off = 0;
  uint32_t alt_label_count = 0;
};

struct AtnDecision {
  uint32_t state_id = 0;
  DecisionKind kind = DecisionKind::ALL;
  uint8_t flags = 0;
  uint16_t alt_count = 0;
  uint16_t k = 0;
  uint32_t table_off = kIndexNoneU32;
  // Decoded index into Atn storage:
  uint32_t decoded_offset = kIndexNoneU32;
};

struct AtnFieldMapEntry {
  uint32_t trans_id = 0;
  lx_field field_id = LX_FIELD_NONE;
  uint16_t flags = 0;
};

struct AtnAltLabelEntry {
  uint16_t rule_id = 0;
  uint16_t alt_index = 0;
  lx_symbol symbol_id = LX_SYMBOL_INVALID;
  uint16_t flags = 0;
  uint32_t entry_state = 0;
};

struct AtnPred {
  uint32_t name_off = 0;
  uint32_t name_len = 0;
  uint32_t flags = 0;
  uint32_t dep_start = 0;
  uint32_t dep_count = 0;
};

struct LlkDfaState {
  uint16_t accept_alt = kTblError;
  uint16_t edge_count = 0;
  uint32_t edge_start = 0;
};

struct LlkDfaEdge {
  lx_symbol symbol = 0;
  uint16_t next_state = 0;
};

struct LlkTableHeader {
  uint32_t state_start = 0;
  uint32_t state_count = 0;
  uint32_t edge_start = 0;
  uint32_t edge_count = 0;
};

/// Decoded and validated ATN + prediction tables (LXATN + LXTBL v1).
class Atn {
 public:
  /// Loads and validates ATN and LXTBL blobs from raw pointers.
  static std::optional<Atn> load(
      const uint8_t* atn_bytes,
      uint32_t atn_size,
      const uint8_t* tables_bytes,
      uint32_t tables_size,
      std::string* error_out = nullptr);

  /// Loads and validates ATN and LXTBL blobs from lx_parser_tables.
  static std::optional<Atn> load(
      const lx_parser_tables& parser_tables,
      std::string* error_out = nullptr);

  /// Validates cross-references against lx_language_info and lx_parser_tables.
  bool validate_against_info(
      const lx_language_info& info,
      const lx_parser_tables& parser_tables,
      std::string* error_out = nullptr) const;

  [[nodiscard]] uint32_t format_version() const noexcept { return format_version_; }
  [[nodiscard]] uint32_t flags() const noexcept { return flags_; }
  [[nodiscard]] uint16_t terminal_count() const noexcept { return terminal_count_; }
  [[nodiscard]] uint16_t start_rule() const noexcept { return start_rule_; }
  [[nodiscard]] uint64_t atn_checksum() const noexcept { return atn_checksum_; }
  [[nodiscard]] uint64_t tables_checksum() const noexcept { return tables_checksum_; }

  [[nodiscard]] std::span<const AtnRule> rules() const noexcept { return rules_; }
  [[nodiscard]] const AtnRule& rule(uint16_t id) const noexcept { return rules_[id]; }

  [[nodiscard]] std::span<const AtnState> states() const noexcept { return states_; }
  [[nodiscard]] const AtnState& state(uint32_t id) const noexcept { return states_[id]; }

  [[nodiscard]] std::span<const AtnTransition> transitions() const noexcept {
    return transitions_;
  }
  [[nodiscard]] std::span<const AtnTransition> state_transitions(
      uint32_t state_id) const noexcept {
    const auto& s = states_[state_id];
    return {transitions_.data() + s.trans_start, s.trans_count};
  }

  [[nodiscard]] std::span<const AtnSet> sets() const noexcept { return sets_; }
  [[nodiscard]] std::span<const AtnInterval> intervals() const noexcept {
    return intervals_;
  }
  [[nodiscard]] std::span<const AtnInterval> set_intervals(uint16_t set_id) const noexcept {
    const auto& s = sets_[set_id];
    return {intervals_.data() + s.interval_start, s.interval_count};
  }
  [[nodiscard]] bool set_contains(uint16_t set_id, lx_symbol sym) const noexcept;

  [[nodiscard]] std::span<const AtnDecision> decisions() const noexcept {
    return decisions_;
  }
  [[nodiscard]] const AtnDecision& decision(uint16_t id) const noexcept {
    return decisions_[id];
  }

  /// Looks up a single lookahead token in an LL(1) decision table.
  /// Returns alt index [0..alt_count-1], kTblError, or kTblAmbig.
  [[nodiscard]] uint16_t ll1_lookup(uint16_t decision_id, lx_symbol sym) const noexcept;

  /// Returns decoded LL(k) DFA states and edges for a LX_DEC_LLK decision.
  [[nodiscard]] std::span<const LlkDfaState> llk_states(uint16_t decision_id) const noexcept;
  [[nodiscard]] std::optional<uint16_t> llk_step(
      uint16_t decision_id,
      uint32_t dfa_state,
      lx_symbol sym) const noexcept;

  [[nodiscard]] std::span<const AtnFieldMapEntry> field_map() const noexcept {
    return field_map_;
  }
  [[nodiscard]] std::span<const AtnFieldMapEntry> rule_field_map(
      uint16_t rule_id) const noexcept {
    const auto& r = rules_[rule_id];
    return {field_map_.data() + r.field_map_off, r.field_map_count};
  }

  [[nodiscard]] std::span<const AtnAltLabelEntry> alt_labels() const noexcept {
    return alt_labels_;
  }
  [[nodiscard]] std::span<const AtnAltLabelEntry> rule_alt_labels(
      uint16_t rule_id) const noexcept {
    const auto& r = rules_[rule_id];
    return {alt_labels_.data() + r.alt_label_off, r.alt_label_count};
  }
  [[nodiscard]] lx_symbol resolve_alt_symbol(
      uint16_t rule_id,
      uint32_t entry_state) const noexcept;

  [[nodiscard]] std::span<const AtnPred> preds() const noexcept { return preds_; }
  [[nodiscard]] std::string_view pred_name(uint32_t pred_id) const noexcept;
  [[nodiscard]] std::span<const uint32_t> pred_deps(uint32_t pred_id) const noexcept;

  [[nodiscard]] uint16_t effective_sync_set(uint32_t state_id) const noexcept;
  [[nodiscard]] bool transition_matches(
      const AtnTransition& tr,
      lx_symbol sym) const noexcept;

 private:
  uint32_t format_version_ = 0;
  uint32_t flags_ = 0;
  uint16_t terminal_count_ = 0;
  uint16_t start_rule_ = 0;
  uint64_t atn_checksum_ = 0;
  uint64_t tables_checksum_ = 0;

  std::string strings_;
  std::vector<AtnState> states_;
  std::vector<AtnTransition> transitions_;
  std::vector<AtnSet> sets_;
  std::vector<AtnInterval> intervals_;
  std::vector<AtnRule> rules_;
  std::vector<AtnDecision> decisions_;
  std::vector<AtnFieldMapEntry> field_map_;
  std::vector<AtnAltLabelEntry> alt_labels_;
  std::vector<AtnPred> preds_;
  std::vector<uint32_t> dep_ids_;

  std::vector<uint16_t> ll1_cells_;
  std::vector<LlkTableHeader> llk_headers_;
  std::vector<LlkDfaState> llk_states_;
  std::vector<LlkDfaEdge> llk_edges_;
};

}  // namespace lx::atn
