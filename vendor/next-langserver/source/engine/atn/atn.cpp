#include "engine/atn/atn.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

#include "engine/common/hash.h"

namespace lx::atn {
namespace {

constexpr uint8_t kAtnMagic[8] = {'L', 'X', 'A', 'T', 'N', 0, 0, 0};
constexpr uint8_t kTblMagic[8] = {'L', 'X', 'T', 'B', 'L', 0, 0, 0};

constexpr uint32_t kAtnHeaderSize = 112;
constexpr uint32_t kTblHeaderSize = 16;
constexpr uint32_t kChecksumSize = 8;

constexpr uint32_t kStateRecordSize = 24;
constexpr uint32_t kTransRecordSize = 20;
constexpr uint32_t kSetDescSize = 8;
constexpr uint32_t kIntervalSize = 4;
constexpr uint32_t kRuleRecordSize = 32;
constexpr uint32_t kDecisionRecordSize = 16;
constexpr uint32_t kFieldMapRecordSize = 8;
constexpr uint32_t kAltLabelRecordSize = 12;
constexpr uint32_t kPredRecordSize = 20;
constexpr uint32_t kLlkHeaderSize = 8;
constexpr uint32_t kLlkStateSize = 8;
constexpr uint32_t kLlkEdgeSize = 4;

bool set_error(std::string* err, std::string_view msg) {
  if (err) {
    *err = std::string(msg);
  }
  return false;
}

[[nodiscard]] uint16_t read_u16_le(const uint8_t* p) noexcept {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

[[nodiscard]] uint32_t read_u32_le(const uint8_t* p) noexcept {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

[[nodiscard]] uint64_t read_u64_le(const uint8_t* p) noexcept {
  return static_cast<uint64_t>(read_u32_le(p)) |
         (static_cast<uint64_t>(read_u32_le(p + 4)) << 32);
}

[[nodiscard]] bool checked_mul_u32(uint32_t a, uint32_t b, uint32_t& out) noexcept {
  uint64_t p = static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
  if (p > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  out = static_cast<uint32_t>(p);
  return true;
}

[[nodiscard]] bool checked_add_u32(uint32_t a, uint32_t b, uint32_t& out) noexcept {
  uint64_t s = static_cast<uint64_t>(a) + static_cast<uint64_t>(b);
  if (s > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  out = static_cast<uint32_t>(s);
  return true;
}

[[nodiscard]] bool check_section_bounds(
    uint32_t off,
    uint32_t byte_len,
    uint32_t min_off,
    uint32_t max_end,
    std::string_view name,
    std::string* err) {
  uint32_t end = 0;
  if (off < min_off || !checked_add_u32(off, byte_len, end) || end > max_end) {
    return set_error(err, std::string("LXATN section out of bounds: ") + std::string(name));
  }
  return true;
}

[[nodiscard]] bool is_valid_state_kind(uint8_t k) noexcept {
  return k >= static_cast<uint8_t>(StateKind::Basic) &&
         k <= static_cast<uint8_t>(StateKind::TokensStart);
}

[[nodiscard]] bool is_valid_trans_kind(uint8_t k) noexcept {
  return k <= static_cast<uint8_t>(TransitionKind::Wildcard);
}

[[nodiscard]] bool is_valid_decision_kind(uint8_t k) noexcept {
  return k <= static_cast<uint8_t>(DecisionKind::ALL);
}

[[nodiscard]] bool is_decision_state_kind(StateKind k) noexcept {
  return k == StateKind::BlockStart || k == StateKind::PlusLoopBack ||
         k == StateKind::StarLoopEntry;
}

}  // namespace

std::optional<Atn> Atn::load(
    const lx_parser_tables& parser_tables,
    std::string* error_out) {
  if (parser_tables.size < sizeof(lx_parser_tables)) {
    set_error(error_out, "lx_parser_tables.size is smaller than expected");
    return std::nullopt;
  }
  if (parser_tables.format_version != LX_ATN_FORMAT_VERSION) {
    set_error(error_out, "lx_parser_tables.format_version mismatch");
    return std::nullopt;
  }
  auto res = load(
      parser_tables.atn,
      parser_tables.atn_size,
      parser_tables.tables,
      parser_tables.tables_size,
      error_out);
  if (!res) {
    return std::nullopt;
  }
  if (parser_tables.start_rule >= res->rules_.size()) {
    set_error(error_out, "lx_parser_tables.start_rule out of range");
    return std::nullopt;
  }
  if (parser_tables.pred_count != res->preds_.size()) {
    set_error(error_out, "lx_parser_tables.pred_count mismatch with LXATN");
    return std::nullopt;
  }
  return res;
}

std::optional<Atn> Atn::load(
    const uint8_t* atn_bytes,
    uint32_t atn_size,
    const uint8_t* tables_bytes,
    uint32_t tables_size,
    std::string* error_out) {
  if (!atn_bytes || atn_size < kAtnHeaderSize + kChecksumSize) {
    set_error(error_out, "LXATN buffer is null or smaller than header + checksum");
    return std::nullopt;
  }
  if (std::memcmp(atn_bytes, kAtnMagic, 8) != 0) {
    set_error(error_out, "LXATN bad magic");
    return std::nullopt;
  }

  const uint32_t format_version = read_u32_le(atn_bytes + 8);
  if (format_version != LX_ATN_FORMAT_VERSION) {
    set_error(error_out, "LXATN unsupported format_version");
    return std::nullopt;
  }

  const uint32_t flags = read_u32_le(atn_bytes + 12);
  const uint16_t terminal_count = read_u16_le(atn_bytes + 16);
  const uint16_t rule_count = read_u16_le(atn_bytes + 18);
  const uint32_t state_count = read_u32_le(atn_bytes + 20);
  const uint32_t trans_count = read_u32_le(atn_bytes + 24);
  const uint32_t set_count = read_u32_le(atn_bytes + 28);
  const uint32_t total_intervals = read_u32_le(atn_bytes + 32);
  const uint32_t decision_count = read_u32_le(atn_bytes + 36);
  const uint32_t field_map_count = read_u32_le(atn_bytes + 40);
  const uint32_t alt_label_count = read_u32_le(atn_bytes + 44);
  const uint32_t pred_count = read_u32_le(atn_bytes + 48);
  const uint32_t total_dep_ids = read_u32_le(atn_bytes + 52);
  const uint32_t string_bytes = read_u32_le(atn_bytes + 56);

  const uint32_t strings_off = read_u32_le(atn_bytes + 60);
  const uint32_t states_off = read_u32_le(atn_bytes + 64);
  const uint32_t trans_off = read_u32_le(atn_bytes + 68);
  const uint32_t sets_off = read_u32_le(atn_bytes + 72);
  const uint32_t rules_off = read_u32_le(atn_bytes + 76);
  const uint32_t decisions_off = read_u32_le(atn_bytes + 80);
  const uint32_t field_map_off = read_u32_le(atn_bytes + 84);
  const uint32_t alt_label_off = read_u32_le(atn_bytes + 88);
  const uint32_t preds_off = read_u32_le(atn_bytes + 92);
  const uint32_t dep_ids_off = read_u32_le(atn_bytes + 96);
  const uint32_t total_size = read_u32_le(atn_bytes + 100);
  const uint16_t start_rule = read_u16_le(atn_bytes + 104);

  if (total_size != atn_size) {
    set_error(error_out, "LXATN total_size does not match atn_size");
    return std::nullopt;
  }

  const uint64_t expected_atn_sum = read_u64_le(atn_bytes + total_size - kChecksumSize);
  const uint64_t actual_atn_sum = lx::fnv1a_64(atn_bytes, total_size - kChecksumSize);
  if (expected_atn_sum != actual_atn_sum) {
    set_error(error_out, "LXATN checksum mismatch");
    return std::nullopt;
  }

  if (terminal_count < LX_FIRST_USER_SYMBOL || terminal_count > 0xFFFDu) {
    set_error(error_out, "LXATN terminal_count out of range [4, 0xFFFD]");
    return std::nullopt;
  }
  if (rule_count == 0 || start_rule >= rule_count) {
    set_error(error_out, "LXATN invalid rule_count or start_rule");
    return std::nullopt;
  }
  if ((string_bytes & 3u) != 0) {
    set_error(error_out, "LXATN string_bytes must be a multiple of 4");
    return std::nullopt;
  }

  const uint32_t payload_end = total_size - kChecksumSize;

  uint32_t states_bytes = 0;
  uint32_t trans_bytes = 0;
  uint32_t set_descs_bytes = 0;
  uint32_t intervals_bytes = 0;
  uint32_t sets_bytes = 0;
  uint32_t rules_bytes = 0;
  uint32_t decisions_bytes = 0;
  uint32_t field_map_bytes = 0;
  uint32_t alt_label_bytes = 0;
  uint32_t preds_bytes = 0;
  uint32_t dep_ids_bytes = 0;

  if (!checked_mul_u32(state_count, kStateRecordSize, states_bytes) ||
      !checked_mul_u32(trans_count, kTransRecordSize, trans_bytes) ||
      !checked_mul_u32(set_count, kSetDescSize, set_descs_bytes) ||
      !checked_mul_u32(total_intervals, kIntervalSize, intervals_bytes) ||
      !checked_add_u32(set_descs_bytes, intervals_bytes, sets_bytes) ||
      !checked_mul_u32(rule_count, kRuleRecordSize, rules_bytes) ||
      !checked_mul_u32(decision_count, kDecisionRecordSize, decisions_bytes) ||
      !checked_mul_u32(field_map_count, kFieldMapRecordSize, field_map_bytes) ||
      !checked_mul_u32(alt_label_count, kAltLabelRecordSize, alt_label_bytes) ||
      !checked_mul_u32(pred_count, kPredRecordSize, preds_bytes) ||
      !checked_mul_u32(total_dep_ids, 4, dep_ids_bytes)) {
    set_error(error_out, "LXATN section size integer overflow");
    return std::nullopt;
  }

  // Validate bounds & canonical non-overlapping order
  if (!check_section_bounds(strings_off, string_bytes, kAtnHeaderSize, payload_end, "strings", error_out) ||
      !check_section_bounds(states_off, states_bytes, strings_off + string_bytes, payload_end, "states", error_out) ||
      !check_section_bounds(trans_off, trans_bytes, states_off + states_bytes, payload_end, "trans", error_out) ||
      !check_section_bounds(sets_off, sets_bytes, trans_off + trans_bytes, payload_end, "sets", error_out) ||
      !check_section_bounds(rules_off, rules_bytes, sets_off + sets_bytes, payload_end, "rules", error_out) ||
      !check_section_bounds(decisions_off, decisions_bytes, rules_off + rules_bytes, payload_end, "decisions", error_out) ||
      !check_section_bounds(field_map_off, field_map_bytes, decisions_off + decisions_bytes, payload_end, "field_map", error_out) ||
      !check_section_bounds(alt_label_off, alt_label_bytes, field_map_off + field_map_bytes, payload_end, "alt_label_map", error_out) ||
      !check_section_bounds(preds_off, preds_bytes, alt_label_off + alt_label_bytes, payload_end, "preds", error_out) ||
      !check_section_bounds(dep_ids_off, dep_ids_bytes, preds_off + preds_bytes, payload_end, "dep_ids", error_out)) {
    return std::nullopt;
  }

  Atn atn;
  atn.format_version_ = format_version;
  atn.flags_ = flags;
  atn.terminal_count_ = terminal_count;
  atn.start_rule_ = start_rule;
  atn.atn_checksum_ = expected_atn_sum;

  atn.strings_.assign(reinterpret_cast<const char*>(atn_bytes + strings_off), string_bytes);

  // Decode sets & intervals
  atn.sets_.resize(set_count);
  for (uint32_t i = 0; i < set_count; ++i) {
    const uint8_t* p = atn_bytes + sets_off + i * kSetDescSize;
    atn.sets_[i].interval_start = read_u32_le(p);
    atn.sets_[i].interval_count = read_u32_le(p + 4);
  }
  atn.intervals_.resize(total_intervals);
  const uint8_t* iv_base = atn_bytes + sets_off + set_descs_bytes;
  for (uint32_t i = 0; i < total_intervals; ++i) {
    const uint8_t* p = iv_base + i * kIntervalSize;
    atn.intervals_[i].lo = read_u16_le(p);
    atn.intervals_[i].hi = read_u16_le(p + 2);
  }
  for (uint32_t i = 0; i < set_count; ++i) {
    const auto& s = atn.sets_[i];
    uint32_t end_iv = 0;
    if (!checked_add_u32(s.interval_start, s.interval_count, end_iv) ||
        end_iv > total_intervals) {
      set_error(error_out, "LXATN set interval range out of bounds");
      return std::nullopt;
    }
    for (uint32_t j = 0; j < s.interval_count; ++j) {
      const auto& iv = atn.intervals_[s.interval_start + j];
      if (iv.lo > iv.hi || iv.hi >= terminal_count) {
        set_error(error_out, "LXATN invalid interval symbol bounds");
        return std::nullopt;
      }
      if (j > 0) {
        const auto& prev = atn.intervals_[s.interval_start + j - 1];
        if (prev.hi >= iv.lo) {
          set_error(error_out, "LXATN set intervals are not strictly increasing");
          return std::nullopt;
        }
      }
    }
  }

  // Decode states
  atn.states_.resize(state_count);
  for (uint32_t i = 0; i < state_count; ++i) {
    const uint8_t* p = atn_bytes + states_off + i * kStateRecordSize;
    const uint8_t raw_kind = p[0];
    if (!is_valid_state_kind(raw_kind)) {
      set_error(error_out, "LXATN invalid state kind");
      return std::nullopt;
    }
    AtnState& st = atn.states_[i];
    st.kind = static_cast<StateKind>(raw_kind);
    st.flags = p[1];
    st.rule_id = read_u16_le(p + 2);
    st.decision_id = read_u16_le(p + 4);
    st.sync_set_id = read_u16_le(p + 6);
    st.trans_start = read_u32_le(p + 8);
    st.trans_count = read_u16_le(p + 12);
    st.alt_index = read_u16_le(p + 14);
    st.pair_state = read_u32_le(p + 16);
    st.loop_end_state = read_u32_le(p + 20);

    if (st.rule_id >= rule_count) {
      set_error(error_out, "LXATN state.rule_id out of bounds");
      return std::nullopt;
    }
    if (st.decision_id != kIndexNoneU16 && st.decision_id >= decision_count) {
      set_error(error_out, "LXATN state.decision_id out of bounds");
      return std::nullopt;
    }
    if (is_decision_state_kind(st.kind) && st.decision_id == kIndexNoneU16) {
      set_error(error_out, "LXATN decision state missing decision_id");
      return std::nullopt;
    }
    if (st.sync_set_id != kIndexNoneU16 && st.sync_set_id >= set_count) {
      set_error(error_out, "LXATN state.sync_set_id out of bounds");
      return std::nullopt;
    }
    uint32_t trans_end = 0;
    if (!checked_add_u32(st.trans_start, st.trans_count, trans_end) ||
        trans_end > trans_count) {
      set_error(error_out, "LXATN state transition range out of bounds");
      return std::nullopt;
    }
    if (st.pair_state != kIndexNoneU32 && st.pair_state >= state_count) {
      set_error(error_out, "LXATN state.pair_state out of bounds");
      return std::nullopt;
    }
    if (st.loop_end_state != kIndexNoneU32 && st.loop_end_state >= state_count) {
      set_error(error_out, "LXATN state.loop_end_state out of bounds");
      return std::nullopt;
    }
  }

  // Decode transitions
  atn.transitions_.resize(trans_count);
  for (uint32_t i = 0; i < trans_count; ++i) {
    const uint8_t* p = atn_bytes + trans_off + i * kTransRecordSize;
    AtnTransition& tr = atn.transitions_[i];
    tr.from = read_u32_le(p);
    tr.to = read_u32_le(p + 4);
    const uint8_t raw_kind = p[8];
    if (!is_valid_trans_kind(raw_kind)) {
      set_error(error_out, "LXATN invalid transition kind");
      return std::nullopt;
    }
    tr.kind = static_cast<TransitionKind>(raw_kind);
    tr.flags = p[9];
    tr.field_id = read_u16_le(p + 10);
    tr.arg0 = read_u32_le(p + 12);
    tr.arg1 = read_u32_le(p + 16);

    if (tr.from >= state_count || tr.to >= state_count) {
      set_error(error_out, "LXATN transition state index out of bounds");
      return std::nullopt;
    }
    const uint16_t from_rule = atn.states_[tr.from].rule_id;
    if (tr.kind != TransitionKind::Rule && atn.states_[tr.to].rule_id != from_rule) {
      set_error(error_out, "LXATN non-rule transition crosses rule boundary");
      return std::nullopt;
    }

    switch (tr.kind) {
      case TransitionKind::Atom:
        if (tr.arg0 >= terminal_count) {
          set_error(error_out, "LXATN Atom transition symbol out of bounds");
          return std::nullopt;
        }
        break;
      case TransitionKind::Set:
      case TransitionKind::NotSet:
        if (tr.arg0 >= set_count) {
          set_error(error_out, "LXATN Set/NotSet transition set_id out of bounds");
          return std::nullopt;
        }
        break;
      case TransitionKind::Range:
        if (tr.arg0 > tr.arg1 || tr.arg1 >= terminal_count) {
          set_error(error_out, "LXATN Range transition bounds invalid");
          return std::nullopt;
        }
        break;
      case TransitionKind::Rule:
        if (tr.arg0 >= rule_count) {
          set_error(error_out, "LXATN Rule transition callee_rule_id out of bounds");
          return std::nullopt;
        }
        if (atn.states_[tr.to].rule_id != from_rule) {
          set_error(error_out, "LXATN Rule transition follow state outside caller rule");
          return std::nullopt;
        }
        break;
      case TransitionKind::Pred:
        if (tr.arg0 >= pred_count) {
          set_error(error_out, "LXATN Pred transition pred_id out of bounds");
          return std::nullopt;
        }
        break;
      case TransitionKind::Epsilon:
      case TransitionKind::Precedence:
      case TransitionKind::Action:
      case TransitionKind::Wildcard:
        break;
    }
  }

  // Verify transitions grouped by state
  std::vector<uint8_t> trans_seen(trans_count, 0);
  for (uint32_t s = 0; s < state_count; ++s) {
    const auto& st = atn.states_[s];
    for (uint32_t k = 0; k < st.trans_count; ++k) {
      const uint32_t tid = st.trans_start + k;
      if (atn.transitions_[tid].from != s) {
        set_error(error_out, "LXATN state transition group has mismatched from");
        return std::nullopt;
      }
      if (trans_seen[tid]) {
        set_error(error_out, "LXATN transition referenced by multiple states");
        return std::nullopt;
      }
      trans_seen[tid] = 1;
    }
  }
  for (uint32_t tid = 0; tid < trans_count; ++tid) {
    if (!trans_seen[tid]) {
      set_error(error_out, "LXATN orphan transition not owned by any state");
      return std::nullopt;
    }
  }

  // Decode rules
  atn.rules_.resize(rule_count);
  for (uint32_t r = 0; r < rule_count; ++r) {
    const uint8_t* p = atn_bytes + rules_off + r * kRuleRecordSize;
    AtnRule& rule = atn.rules_[r];
    rule.start_state = read_u32_le(p);
    rule.stop_state = read_u32_le(p + 4);
    rule.symbol_id = read_u16_le(p + 8);
    rule.flags = read_u16_le(p + 10);
    rule.max_prec = read_u16_le(p + 12);
    rule.sync_set_id = read_u16_le(p + 14);
    rule.field_map_off = read_u32_le(p + 16);
    rule.field_map_count = read_u32_le(p + 20);
    rule.alt_label_off = read_u32_le(p + 24);
    rule.alt_label_count = read_u32_le(p + 28);

    if (rule.start_state >= state_count || rule.stop_state >= state_count) {
      set_error(error_out, "LXATN rule start/stop state out of bounds");
      return std::nullopt;
    }
    if (atn.states_[rule.start_state].kind != StateKind::RuleStart ||
        atn.states_[rule.start_state].rule_id != r) {
      set_error(error_out, "LXATN rule.start_state is not RULE_START of rule");
      return std::nullopt;
    }
    if (atn.states_[rule.stop_state].kind != StateKind::RuleStop ||
        atn.states_[rule.stop_state].rule_id != r) {
      set_error(error_out, "LXATN rule.stop_state is not RULE_STOP of rule");
      return std::nullopt;
    }
    if (rule.sync_set_id != kIndexNoneU16 && rule.sync_set_id >= set_count) {
      set_error(error_out, "LXATN rule.sync_set_id out of bounds");
      return std::nullopt;
    }
    uint32_t fm_end = 0;
    if (!checked_add_u32(rule.field_map_off, rule.field_map_count, fm_end) ||
        fm_end > field_map_count) {
      set_error(error_out, "LXATN rule field_map range out of bounds");
      return std::nullopt;
    }
    uint32_t al_end = 0;
    if (!checked_add_u32(rule.alt_label_off, rule.alt_label_count, al_end) ||
        al_end > alt_label_count) {
      set_error(error_out, "LXATN rule alt_label range out of bounds");
      return std::nullopt;
    }
  }

  // Per-rule reachability validation (BFS from start_state within rule)
  std::vector<uint8_t> visited(state_count, 0);
  std::vector<uint32_t> queue;
  for (uint32_t r = 0; r < rule_count; ++r) {
    queue.clear();
    const uint32_t start = atn.rules_[r].start_state;
    visited[start] = 1;
    queue.push_back(start);
    for (size_t qi = 0; qi < queue.size(); ++qi) {
      const uint32_t cur = queue[qi];
      const auto& st = atn.states_[cur];
      for (uint32_t k = 0; k < st.trans_count; ++k) {
        const uint32_t next = atn.transitions_[st.trans_start + k].to;
        if (!visited[next]) {
          visited[next] = 1;
          queue.push_back(next);
        }
      }
    }
    if (!visited[atn.rules_[r].stop_state]) {
      set_error(error_out, "LXATN rule stop_state unreachable from start_state");
      return std::nullopt;
    }
  }
  for (uint32_t s = 0; s < state_count; ++s) {
    if (!visited[s]) {
      set_error(error_out, "LXATN unreachable state in rule");
      return std::nullopt;
    }
  }

  // Decode field_map
  atn.field_map_.resize(field_map_count);
  for (uint32_t i = 0; i < field_map_count; ++i) {
    const uint8_t* p = atn_bytes + field_map_off + i * kFieldMapRecordSize;
    AtnFieldMapEntry& fm = atn.field_map_[i];
    fm.trans_id = read_u32_le(p);
    fm.field_id = read_u16_le(p + 4);
    fm.flags = read_u16_le(p + 6);
    if (fm.trans_id >= trans_count || fm.field_id == LX_FIELD_NONE) {
      set_error(error_out, "LXATN invalid field_map entry");
      return std::nullopt;
    }
    if (atn.transitions_[fm.trans_id].field_id != fm.field_id) {
      set_error(error_out, "LXATN field_map entry disagrees with transition field_id");
      return std::nullopt;
    }
  }
  for (uint32_t r = 0; r < rule_count; ++r) {
    for (const auto& fm : atn.rule_field_map(static_cast<uint16_t>(r))) {
      const uint32_t from_state = atn.transitions_[fm.trans_id].from;
      if (atn.states_[from_state].rule_id != r) {
        set_error(error_out, "LXATN field_map entry belongs to different rule");
        return std::nullopt;
      }
    }
  }

  // Decode alt_label_map
  atn.alt_labels_.resize(alt_label_count);
  for (uint32_t i = 0; i < alt_label_count; ++i) {
    const uint8_t* p = atn_bytes + alt_label_off + i * kAltLabelRecordSize;
    AtnAltLabelEntry& al = atn.alt_labels_[i];
    al.rule_id = read_u16_le(p);
    al.alt_index = read_u16_le(p + 2);
    al.symbol_id = read_u16_le(p + 4);
    al.flags = read_u16_le(p + 6);
    al.entry_state = read_u32_le(p + 8);
    if (al.rule_id >= rule_count || al.entry_state >= state_count ||
        atn.states_[al.entry_state].rule_id != al.rule_id) {
      set_error(error_out, "LXATN invalid alt_label_map entry");
      return std::nullopt;
    }
  }
  for (uint32_t r = 0; r < rule_count; ++r) {
    for (const auto& al : atn.rule_alt_labels(static_cast<uint16_t>(r))) {
      if (al.rule_id != r) {
        set_error(error_out, "LXATN alt_label_map entry rule_id mismatch");
        return std::nullopt;
      }
    }
  }

  // Decode preds & dep_ids
  atn.preds_.resize(pred_count);
  for (uint32_t i = 0; i < pred_count; ++i) {
    const uint8_t* p = atn_bytes + preds_off + i * kPredRecordSize;
    AtnPred& pr = atn.preds_[i];
    pr.name_off = read_u32_le(p);
    pr.name_len = read_u32_le(p + 4);
    pr.flags = read_u32_le(p + 8);
    pr.dep_start = read_u32_le(p + 12);
    pr.dep_count = read_u32_le(p + 16);

    uint32_t name_end = 0;
    uint32_t dep_end = 0;
    if (!checked_add_u32(pr.name_off, pr.name_len, name_end) ||
        name_end > string_bytes ||
        !checked_add_u32(pr.dep_start, pr.dep_count, dep_end) ||
        dep_end > total_dep_ids) {
      set_error(error_out, "LXATN predicate string or dep range out of bounds");
      return std::nullopt;
    }
  }
  atn.dep_ids_.resize(total_dep_ids);
  for (uint32_t i = 0; i < total_dep_ids; ++i) {
    atn.dep_ids_[i] = read_u32_le(atn_bytes + dep_ids_off + i * 4);
  }

  // Validate LXTBL if present
  bool has_tables = (tables_bytes != nullptr && tables_size > 0);
  if (has_tables) {
    if (tables_size < kTblHeaderSize + kChecksumSize) {
      set_error(error_out, "LXTBL buffer smaller than header + checksum");
      return std::nullopt;
    }
    if (std::memcmp(tables_bytes, kTblMagic, 8) != 0) {
      set_error(error_out, "LXTBL bad magic");
      return std::nullopt;
    }
    if (read_u32_le(tables_bytes + 8) != LX_ATN_FORMAT_VERSION) {
      set_error(error_out, "LXTBL unsupported format_version");
      return std::nullopt;
    }
    const uint32_t tbl_total = read_u32_le(tables_bytes + 12);
    if (tbl_total != tables_size) {
      set_error(error_out, "LXTBL total_size mismatch");
      return std::nullopt;
    }
    const uint64_t expected_tbl_sum = read_u64_le(tables_bytes + tbl_total - kChecksumSize);
    const uint64_t actual_tbl_sum = lx::fnv1a_64(tables_bytes, tbl_total - kChecksumSize);
    if (expected_tbl_sum != actual_tbl_sum) {
      set_error(error_out, "LXTBL checksum mismatch");
      return std::nullopt;
    }
    atn.tables_checksum_ = expected_tbl_sum;
  }

  // Decode decisions and prediction tables
  atn.decisions_.resize(decision_count);
  const uint32_t tbl_payload_end = has_tables ? (tables_size - kChecksumSize) : 0;
  std::unordered_map<uint32_t, uint32_t> ll1_cache;
  std::unordered_map<uint32_t, uint32_t> llk_cache;

  for (uint32_t i = 0; i < decision_count; ++i) {
    const uint8_t* p = atn_bytes + decisions_off + i * kDecisionRecordSize;
    AtnDecision& d = atn.decisions_[i];
    d.state_id = read_u32_le(p);
    const uint8_t raw_kind = p[4];
    if (!is_valid_decision_kind(raw_kind)) {
      set_error(error_out, "LXATN invalid decision kind");
      return std::nullopt;
    }
    d.kind = static_cast<DecisionKind>(raw_kind);
    d.flags = p[5];
    d.alt_count = read_u16_le(p + 6);
    d.k = read_u16_le(p + 8);
    d.table_off = read_u32_le(p + 12);

    if (d.state_id >= state_count ||
        atn.states_[d.state_id].decision_id != i ||
        d.alt_count == 0 ||
        d.alt_count != atn.states_[d.state_id].trans_count) {
      set_error(error_out, "LXATN decision/state consistency failure");
      return std::nullopt;
    }

    if (d.kind == DecisionKind::ALL) {
      if (d.table_off != kIndexNoneU32) {
        set_error(error_out, "LXATN ALL decision must have table_off = 0xFFFFFFFF");
        return std::nullopt;
      }
      continue;
    }

    if (!has_tables) {
      set_error(error_out, "LXATN LL(1)/LL(k) decision requires non-empty LXTBL");
      return std::nullopt;
    }
    if (d.table_off < kTblHeaderSize || (d.table_off & 3u) != 0) {
      set_error(error_out, "LXTBL decision table_off invalid or unaligned");
      return std::nullopt;
    }

    if (d.kind == DecisionKind::LL1) {
      if (d.k != 1) {
        set_error(error_out, "LXATN LL1 decision must have k == 1");
        return std::nullopt;
      }
      uint32_t table_bytes = static_cast<uint32_t>(terminal_count) * 2u;
      uint32_t table_end = 0;
      if (!checked_add_u32(d.table_off, table_bytes, table_end) ||
          table_end > tbl_payload_end) {
        set_error(error_out, "LXTBL LL1 table out of bounds");
        return std::nullopt;
      }
      auto it = ll1_cache.find(d.table_off);
      if (it != ll1_cache.end()) {
        d.decoded_offset = it->second;
      } else {
        const uint32_t start_idx = static_cast<uint32_t>(atn.ll1_cells_.size());
        atn.ll1_cells_.resize(start_idx + terminal_count);
        for (uint16_t c = 0; c < terminal_count; ++c) {
          const uint16_t val = read_u16_le(tables_bytes + d.table_off + c * 2u);
          if (val != kTblError && val != kTblAmbig && val >= d.alt_count) {
            set_error(error_out, "LXTBL LL1 entry out of alt_count bounds");
            return std::nullopt;
          }
          atn.ll1_cells_[start_idx + c] = val;
        }
        d.decoded_offset = start_idx;
        ll1_cache.emplace(d.table_off, start_idx);
      }
    } else {
      // DecisionKind::LLK
      if (d.k < 2) {
        set_error(error_out, "LXATN LLK decision must have k >= 2");
        return std::nullopt;
      }
      uint32_t hdr_end = 0;
      if (!checked_add_u32(d.table_off, kLlkHeaderSize, hdr_end) ||
          hdr_end > tbl_payload_end) {
        set_error(error_out, "LXTBL LLK header out of bounds");
        return std::nullopt;
      }
      const uint32_t dfa_states = read_u32_le(tables_bytes + d.table_off);
      const uint32_t dfa_edges = read_u32_le(tables_bytes + d.table_off + 4);
      if (dfa_states == 0) {
        set_error(error_out, "LXTBL LLK dfa_state_count must be >= 1");
        return std::nullopt;
      }
      uint32_t states_bytes_len = 0;
      uint32_t edges_bytes_len = 0;
      uint32_t body_bytes = 0;
      uint32_t table_end = 0;
      if (!checked_mul_u32(dfa_states, kLlkStateSize, states_bytes_len) ||
          !checked_mul_u32(dfa_edges, kLlkEdgeSize, edges_bytes_len) ||
          !checked_add_u32(states_bytes_len, edges_bytes_len, body_bytes) ||
          !checked_add_u32(hdr_end, body_bytes, table_end) ||
          table_end > tbl_payload_end) {
        set_error(error_out, "LXTBL LLK table out of bounds");
        return std::nullopt;
      }

      auto it = llk_cache.find(d.table_off);
      if (it != llk_cache.end()) {
        d.decoded_offset = it->second;
      } else {
        LlkTableHeader hdr;
        hdr.state_start = static_cast<uint32_t>(atn.llk_states_.size());
        hdr.state_count = dfa_states;
        hdr.edge_start = static_cast<uint32_t>(atn.llk_edges_.size());
        hdr.edge_count = dfa_edges;

        atn.llk_states_.resize(hdr.state_start + dfa_states);
        const uint8_t* st_ptr = tables_bytes + hdr_end;
        for (uint32_t s = 0; s < dfa_states; ++s) {
          const uint8_t* sp = st_ptr + s * kLlkStateSize;
          LlkDfaState dst;
          dst.accept_alt = read_u16_le(sp);
          dst.edge_count = read_u16_le(sp + 2);
          dst.edge_start = read_u32_le(sp + 4);
          if (dst.accept_alt != kTblError && dst.accept_alt != kTblAmbig &&
              dst.accept_alt >= d.alt_count) {
            set_error(error_out, "LXTBL LLK accept_alt out of bounds");
            return std::nullopt;
          }
          uint32_t e_end = 0;
          if (!checked_add_u32(dst.edge_start, dst.edge_count, e_end) ||
              e_end > dfa_edges) {
            set_error(error_out, "LXTBL LLK state edge range out of bounds");
            return std::nullopt;
          }
          atn.llk_states_[hdr.state_start + s] = dst;
        }

        atn.llk_edges_.resize(hdr.edge_start + dfa_edges);
        const uint8_t* ed_ptr = st_ptr + states_bytes_len;
        for (uint32_t e = 0; e < dfa_edges; ++e) {
          const uint8_t* ep = ed_ptr + e * kLlkEdgeSize;
          LlkDfaEdge ded;
          ded.symbol = read_u16_le(ep);
          ded.next_state = read_u16_le(ep + 2);
          if (ded.symbol >= terminal_count || ded.next_state >= dfa_states) {
            set_error(error_out, "LXTBL LLK edge symbol or next_state out of bounds");
            return std::nullopt;
          }
          atn.llk_edges_[hdr.edge_start + e] = ded;
        }

        // Verify sorted unique edge symbols per DFA state
        for (uint32_t s = 0; s < dfa_states; ++s) {
          const auto& dst = atn.llk_states_[hdr.state_start + s];
          for (uint32_t j = 1; j < dst.edge_count; ++j) {
            const auto& prev = atn.llk_edges_[hdr.edge_start + dst.edge_start + j - 1];
            const auto& curr = atn.llk_edges_[hdr.edge_start + dst.edge_start + j];
            if (prev.symbol >= curr.symbol) {
              set_error(error_out, "LXTBL LLK edges must be strictly sorted by symbol");
              return std::nullopt;
            }
          }
        }

        const uint32_t hdr_idx = static_cast<uint32_t>(atn.llk_headers_.size());
        atn.llk_headers_.push_back(hdr);
        d.decoded_offset = hdr_idx;
        llk_cache.emplace(d.table_off, hdr_idx);
      }
    }
  }

  // Verify every state with decision_id != 0xFFFF points to a decision whose state_id == s
  for (uint32_t s = 0; s < state_count; ++s) {
    const uint16_t did = atn.states_[s].decision_id;
    if (did != kIndexNoneU16 && atn.decisions_[did].state_id != s) {
      set_error(error_out, "LXATN state decision_id back-pointer mismatch");
      return std::nullopt;
    }
  }

  return atn;
}

bool Atn::validate_against_info(
    const lx_language_info& info,
    const lx_parser_tables& parser_tables,
    std::string* error_out) const {
  if (info.size < sizeof(lx_language_info)) {
    return set_error(error_out, "lx_language_info.size too small");
  }
  if (terminal_count_ > info.symbol_count) {
    return set_error(error_out, "ATN terminal_count exceeds lx_language_info.symbol_count");
  }
  for (const auto& r : rules_) {
    if (r.symbol_id >= info.symbol_count) {
      return set_error(error_out, "ATN rule symbol_id exceeds lx_language_info.symbol_count");
    }
  }
  for (const auto& tr : transitions_) {
    if (tr.field_id != LX_FIELD_NONE && tr.field_id >= info.field_count) {
      return set_error(error_out, "ATN transition field_id exceeds lx_language_info.field_count");
    }
  }
  for (const auto& fm : field_map_) {
    if (fm.field_id >= info.field_count) {
      return set_error(error_out, "ATN field_map field_id exceeds lx_language_info.field_count");
    }
  }
  for (const auto& al : alt_labels_) {
    if (al.symbol_id >= info.symbol_count) {
      return set_error(error_out, "ATN alt_label symbol_id exceeds lx_language_info.symbol_count");
    }
  }
  if (parser_tables.pred_count != preds_.size()) {
    return set_error(error_out, "lx_parser_tables.pred_count mismatch");
  }
  if (parser_tables.pred_count > 0 && parser_tables.pred_names) {
    for (uint32_t i = 0; i < parser_tables.pred_count; ++i) {
      const lx_str& s = parser_tables.pred_names[i];
      const std::string_view info_name =
          s.ptr ? std::string_view(s.ptr, s.len) : std::string_view{};
      if (info_name != pred_name(i)) {
        return set_error(error_out, "ATN predicate name mismatch with lx_parser_tables");
      }
    }
  }
  return true;
}

bool Atn::set_contains(uint16_t set_id, lx_symbol sym) const noexcept {
  const auto ivs = set_intervals(set_id);
  size_t lo = 0;
  size_t hi = ivs.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const auto& iv = ivs[mid];
    if (sym < iv.lo) {
      hi = mid;
    } else if (sym > iv.hi) {
      lo = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

uint16_t Atn::ll1_lookup(uint16_t decision_id, lx_symbol sym) const noexcept {
  if (decision_id >= decisions_.size()) {
    return kTblError;
  }
  const auto& d = decisions_[decision_id];
  if (d.kind != DecisionKind::LL1 || sym >= terminal_count_) {
    return kTblError;
  }
  return ll1_cells_[d.decoded_offset + sym];
}

std::span<const LlkDfaState> Atn::llk_states(uint16_t decision_id) const noexcept {
  if (decision_id >= decisions_.size()) {
    return {};
  }
  const auto& d = decisions_[decision_id];
  if (d.kind != DecisionKind::LLK || d.decoded_offset >= llk_headers_.size()) {
    return {};
  }
  const auto& h = llk_headers_[d.decoded_offset];
  return {llk_states_.data() + h.state_start, h.state_count};
}

std::optional<uint16_t> Atn::llk_step(
    uint16_t decision_id,
    uint32_t dfa_state,
    lx_symbol sym) const noexcept {
  if (decision_id >= decisions_.size()) {
    return std::nullopt;
  }
  const auto& d = decisions_[decision_id];
  if (d.kind != DecisionKind::LLK || d.decoded_offset >= llk_headers_.size()) {
    return std::nullopt;
  }
  const auto& h = llk_headers_[d.decoded_offset];
  if (dfa_state >= h.state_count) {
    return std::nullopt;
  }
  const auto& st = llk_states_[h.state_start + dfa_state];
  const LlkDfaEdge* edges = llk_edges_.data() + h.edge_start + st.edge_start;
  size_t lo = 0;
  size_t hi = st.edge_count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (edges[mid].symbol < sym) {
      lo = mid + 1;
    } else if (edges[mid].symbol > sym) {
      hi = mid;
    } else {
      return edges[mid].next_state;
    }
  }
  return std::nullopt;
}

lx_symbol Atn::resolve_alt_symbol(
    uint16_t rule_id,
    uint32_t entry_state) const noexcept {
  for (const auto& al : rule_alt_labels(rule_id)) {
    if (al.entry_state == entry_state) {
      return al.symbol_id;
    }
  }
  return rules_[rule_id].symbol_id;
}

std::string_view Atn::pred_name(uint32_t pred_id) const noexcept {
  const auto& pr = preds_[pred_id];
  return std::string_view(strings_.data() + pr.name_off, pr.name_len);
}

std::span<const uint32_t> Atn::pred_deps(uint32_t pred_id) const noexcept {
  const auto& pr = preds_[pred_id];
  return {dep_ids_.data() + pr.dep_start, pr.dep_count};
}

uint16_t Atn::effective_sync_set(uint32_t state_id) const noexcept {
  const auto& st = states_[state_id];
  if (st.sync_set_id != kIndexNoneU16) {
    return st.sync_set_id;
  }
  return rules_[st.rule_id].sync_set_id;
}

bool Atn::transition_matches(
    const AtnTransition& tr,
    lx_symbol sym) const noexcept {
  switch (tr.kind) {
    case TransitionKind::Atom:
      return sym == static_cast<lx_symbol>(tr.arg0);
    case TransitionKind::Set:
      return set_contains(static_cast<uint16_t>(tr.arg0), sym);
    case TransitionKind::NotSet:
      return sym >= LX_FIRST_USER_SYMBOL && sym < terminal_count_ &&
             !set_contains(static_cast<uint16_t>(tr.arg0), sym);
    case TransitionKind::Range:
      return sym >= static_cast<lx_symbol>(tr.arg0) &&
             sym <= static_cast<lx_symbol>(tr.arg1);
    case TransitionKind::Wildcard:
      return sym >= LX_FIRST_USER_SYMBOL && sym < terminal_count_;
    default:
      return false;
  }
}

}  // namespace lx::atn
