#include "engine/atn/atn.h"
#include "engine/common/hash.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace {

void push_u16(std::vector<uint8_t>& buf, uint16_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}

void push_u32(std::vector<uint8_t>& buf, uint32_t v) {
  buf.push_back(static_cast<uint8_t>(v & 0xFFu));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void push_u64(std::vector<uint8_t>& buf, uint64_t v) {
  push_u32(buf, static_cast<uint32_t>(v & 0xFFFFFFFFu));
  push_u32(buf, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

void write_u32_at(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
  buf[off + 0] = static_cast<uint8_t>(v & 0xFFu);
  buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
  buf[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
  buf[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void write_u64_at(std::vector<uint8_t>& buf, size_t off, uint64_t v) {
  write_u32_at(buf, off, static_cast<uint32_t>(v & 0xFFFFFFFFu));
  write_u32_at(buf, off + 4, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

struct TestAtnFixture {
  std::vector<uint8_t> atn;
  std::vector<uint8_t> tables;
};

// Builds a valid 2-rule ATN with:
// - rule 0 (symbol 6, START): BlockStart (LL1 decision 0) -> alt0 (Atom 4, field 1) or alt1 (LLK decision 1) -> Rule(rule 1) -> Stop
// - rule 1 (symbol 7): StarLoopEntry (ALL decision 2, LOOP_SPLICEABLE) -> Atom 5 -> LoopBack -> LoopEnd -> Stop
// - 1 interval set [4..5], 1 predicate "is_ok" with dep_id 42, alt_label_map, field_map
TestAtnFixture build_sample_fixture() {
  using namespace lx::atn;

  // Strings: "is_ok" (len 5) padded to 8
  std::string strings = "is_ok";
  while (strings.size() % 4 != 0) {
    strings.push_back('\0');
  }

  const uint16_t terminal_count = 6;  // 0..3 reserved, 4=ID, 5=NUM

  // Build LXTBL first so we know table_off values
  std::vector<uint8_t> tbl;
  // Magic + version + total_size
  const char tbl_magic[8] = {'L', 'X', 'T', 'B', 'L', 0, 0, 0};
  tbl.insert(tbl.end(), tbl_magic, tbl_magic + 8);
  push_u32(tbl, LX_ATN_FORMAT_VERSION);
  push_u32(tbl, 0);  // total_size placeholder

  // Decision 0: LL1 table at offset 16 (6 * 2 = 12 bytes)
  const uint32_t ll1_off = static_cast<uint32_t>(tbl.size());
  push_u16(tbl, kTblError);  // 0: INVALID
  push_u16(tbl, 1);          // 1: EOF -> alt 1
  push_u16(tbl, kTblError);  // 2: ERROR
  push_u16(tbl, kTblError);  // 3: MISSING
  push_u16(tbl, 0);          // 4: ID -> alt 0
  push_u16(tbl, 1);          // 5: NUM -> alt 1

  // Decision 1: LLK table (k=2) at offset 28 (multiple of 4!)
  const uint32_t llk_off = static_cast<uint32_t>(tbl.size());
  // 2 states, 2 edges
  push_u32(tbl, 2);  // dfa_state_count
  push_u32(tbl, 2);  // edge_count
  // state 0: non-accept, 2 edges from 0
  push_u16(tbl, kTblError);
  push_u16(tbl, 2);
  push_u32(tbl, 0);
  // state 1: accept alt 0, 0 edges
  push_u16(tbl, 0);
  push_u16(tbl, 0);
  push_u32(tbl, 0);
  // edges (sorted by symbol): (EOF->1), (NUM->1)
  push_u16(tbl, LX_SYMBOL_EOF);
  push_u16(tbl, 1);
  push_u16(tbl, 5);
  push_u16(tbl, 1);

  const uint32_t tbl_total = static_cast<uint32_t>(tbl.size() + 8);
  write_u32_at(tbl, 12, tbl_total);
  const uint64_t tbl_sum = lx::fnv1a_64(tbl.data(), tbl.size());
  push_u64(tbl, tbl_sum);

  // States:
  // Rule 0: 0=RuleStart, 1=BlockStart(dec 0), 2=Basic(alt0), 3=BlockStart(dec 1), 4=Basic(alt1), 5=BlockEnd, 6=RuleStop
  // Rule 1: 7=RuleStart, 8=StarLoopEntry(dec 2), 9=StarBlockStart, 10=StarLoopBack, 11=LoopEnd, 12=RuleStop
  std::vector<AtnState> states(13);
  std::vector<AtnTransition> trans;

  auto add_tr = [&](uint32_t from, uint32_t to, TransitionKind k, uint8_t fl,
                    lx_field fld, uint32_t a0, uint32_t a1) -> uint32_t {
    uint32_t id = static_cast<uint32_t>(trans.size());
    trans.push_back({from, to, k, fl, fld, a0, a1});
    return id;
  };

  // State 0: RuleStart (rule 0)
  states[0] = {StateKind::RuleStart, 0, 0, kIndexNoneU16, 0, 0, 1, kIndexNoneU16, 6, kIndexNoneU32};
  add_tr(0, 1, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 1: BlockStart (dec 0)
  states[1] = {StateKind::BlockStart, 0, 0, 0, kIndexNoneU16, 1, 2, kIndexNoneU16, 5, kIndexNoneU32};
  add_tr(1, 2, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);
  add_tr(1, 3, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 2: Basic (Atom 4, field 1)
  states[2] = {StateKind::Basic, 0, 0, kIndexNoneU16, kIndexNoneU16, 3, 1, 0, kIndexNoneU32, kIndexNoneU32};
  const uint32_t field_trans_id = add_tr(2, 5, TransitionKind::Atom, 0, 1, 4, 0);

  // State 3: BlockStart (dec 1, LLK) -> 4
  states[3] = {StateKind::BlockStart, 0, 0, 1, kIndexNoneU16, 4, 1, 1, 5, kIndexNoneU32};
  add_tr(3, 4, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 4: Basic -> Rule(1) -> 5
  states[4] = {StateKind::Basic, 0, 0, kIndexNoneU16, kIndexNoneU16, 5, 1, kIndexNoneU16, kIndexNoneU32, kIndexNoneU32};
  add_tr(4, 5, TransitionKind::Rule, 0, LX_FIELD_NONE, 1, 0);

  // State 5: BlockEnd -> 6
  states[5] = {StateKind::BlockEnd, 0, 0, kIndexNoneU16, kIndexNoneU16, 6, 1, kIndexNoneU16, kIndexNoneU32, kIndexNoneU32};
  add_tr(5, 6, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 6: RuleStop (rule 0)
  states[6] = {StateKind::RuleStop, 0, 0, kIndexNoneU16, kIndexNoneU16, 7, 0, kIndexNoneU16, kIndexNoneU32, kIndexNoneU32};

  // State 7: RuleStart (rule 1)
  states[7] = {StateKind::RuleStart, 0, 1, kIndexNoneU16, 0, 7, 1, kIndexNoneU16, 12, kIndexNoneU32};
  add_tr(7, 8, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 8: StarLoopEntry (dec 2, LOOP_SPLICEABLE)
  states[8] = {StateKind::StarLoopEntry, kStateLoopSpliceable, 1, 2, kIndexNoneU16, 8, 2, kIndexNoneU16, 10, 11};
  add_tr(8, 9, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);
  add_tr(8, 11, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 9: StarBlockStart -> Pred(0) -> Atom(5) via 10
  states[9] = {StateKind::StarBlockStart, 0, 1, kIndexNoneU16, kIndexNoneU16, 10, 1, kIndexNoneU16, 10, kIndexNoneU32};
  add_tr(9, 10, TransitionKind::Set, 0, LX_FIELD_NONE, 0, 0);

  // State 10: StarLoopBack -> 8
  states[10] = {StateKind::StarLoopBack, 0, 1, kIndexNoneU16, kIndexNoneU16, 11, 1, kIndexNoneU16, 8, kIndexNoneU32};
  add_tr(10, 8, TransitionKind::Pred, 0, LX_FIELD_NONE, 0, 0);

  // State 11: LoopEnd -> 12
  states[11] = {StateKind::LoopEnd, 0, 1, kIndexNoneU16, kIndexNoneU16, 12, 1, kIndexNoneU16, kIndexNoneU32, kIndexNoneU32};
  add_tr(11, 12, TransitionKind::Epsilon, 0, LX_FIELD_NONE, 0, 0);

  // State 12: RuleStop (rule 1)
  states[12] = {StateKind::RuleStop, 0, 1, kIndexNoneU16, kIndexNoneU16, 13, 0, kIndexNoneU16, kIndexNoneU32, kIndexNoneU32};

  // Serialize LXATN
  std::vector<uint8_t> atn;
  const char atn_magic[8] = {'L', 'X', 'A', 'T', 'N', 0, 0, 0};
  atn.insert(atn.end(), atn_magic, atn_magic + 8);
  push_u32(atn, LX_ATN_FORMAT_VERSION);
  push_u32(atn, 0);  // flags
  push_u16(atn, terminal_count);
  push_u16(atn, 2);  // rule_count
  push_u32(atn, static_cast<uint32_t>(states.size()));
  push_u32(atn, static_cast<uint32_t>(trans.size()));
  push_u32(atn, 1);  // set_count
  push_u32(atn, 2);  // total_intervals: [1,1] and [4,5]
  push_u32(atn, 3);  // decision_count
  push_u32(atn, 1);  // field_map_count
  push_u32(atn, 2);  // alt_label_count
  push_u32(atn, 1);  // pred_count
  push_u32(atn, 1);  // total_dep_ids
  push_u32(atn, static_cast<uint32_t>(strings.size()));

  const size_t off_table_pos = atn.size();
  for (int i = 0; i < 11; ++i) {
    push_u32(atn, 0);  // 10 section offsets + total_size
  }
  push_u16(atn, 0);  // start_rule
  push_u16(atn, 0);  // reserved0
  push_u32(atn, 0);  // reserved1
  assert(atn.size() == 112);

  // 1. strings
  const uint32_t strings_off = static_cast<uint32_t>(atn.size());
  atn.insert(atn.end(), strings.begin(), strings.end());

  // 2. states
  const uint32_t states_off = static_cast<uint32_t>(atn.size());
  for (const auto& s : states) {
    atn.push_back(static_cast<uint8_t>(s.kind));
    atn.push_back(s.flags);
    push_u16(atn, s.rule_id);
    push_u16(atn, s.decision_id);
    push_u16(atn, s.sync_set_id);
    push_u32(atn, s.trans_start);
    push_u16(atn, s.trans_count);
    push_u16(atn, s.alt_index);
    push_u32(atn, s.pair_state);
    push_u32(atn, s.loop_end_state);
  }

  // 3. trans
  const uint32_t trans_off = static_cast<uint32_t>(atn.size());
  for (const auto& t : trans) {
    push_u32(atn, t.from);
    push_u32(atn, t.to);
    atn.push_back(static_cast<uint8_t>(t.kind));
    atn.push_back(t.flags);
    push_u16(atn, t.field_id);
    push_u32(atn, t.arg0);
    push_u32(atn, t.arg1);
  }

  // 4. sets: 1 set with 2 intervals ([1,1], [4,5])
  const uint32_t sets_off = static_cast<uint32_t>(atn.size());
  push_u32(atn, 0);  // interval_start
  push_u32(atn, 2);  // interval_count
  push_u16(atn, LX_SYMBOL_EOF);
  push_u16(atn, LX_SYMBOL_EOF);
  push_u16(atn, 4);
  push_u16(atn, 5);

  // 5. rules
  const uint32_t rules_off = static_cast<uint32_t>(atn.size());
  // rule 0
  push_u32(atn, 0);  // start_state
  push_u32(atn, 6);  // stop_state
  push_u16(atn, 6);  // symbol_id
  push_u16(atn, kRuleStart);
  push_u16(atn, 0);  // max_prec
  push_u16(atn, 0);  // sync_set_id
  push_u32(atn, 0);  // field_map_off
  push_u32(atn, 1);  // field_map_count
  push_u32(atn, 0);  // alt_label_off
  push_u32(atn, 2);  // alt_label_count
  // rule 1
  push_u32(atn, 7);   // start_state
  push_u32(atn, 12);  // stop_state
  push_u16(atn, 7);   // symbol_id
  push_u16(atn, kRuleLoopSpliceable | kRuleListItem);
  push_u16(atn, 0);  // max_prec
  push_u16(atn, 0);  // sync_set_id
  push_u32(atn, 1);  // field_map_off
  push_u32(atn, 0);  // field_map_count
  push_u32(atn, 2);  // alt_label_off
  push_u32(atn, 0);  // alt_label_count

  // 6. decisions
  const uint32_t decisions_off = static_cast<uint32_t>(atn.size());
  // dec 0: LL1
  push_u32(atn, 1);
  atn.push_back(static_cast<uint8_t>(DecisionKind::LL1));
  atn.push_back(0);
  push_u16(atn, 2);
  push_u16(atn, 1);
  push_u16(atn, 0);
  push_u32(atn, ll1_off);
  // dec 1: LLK
  push_u32(atn, 3);
  atn.push_back(static_cast<uint8_t>(DecisionKind::LLK));
  atn.push_back(0);
  push_u16(atn, 1);
  push_u16(atn, 2);
  push_u16(atn, 0);
  push_u32(atn, llk_off);
  // dec 2: ALL (has pred)
  push_u32(atn, 8);
  atn.push_back(static_cast<uint8_t>(DecisionKind::ALL));
  atn.push_back(kDecHasPred);
  push_u16(atn, 2);
  push_u16(atn, 0);
  push_u16(atn, 0);
  push_u32(atn, kIndexNoneU32);

  // 7. field_map
  const uint32_t field_map_off = static_cast<uint32_t>(atn.size());
  push_u32(atn, field_trans_id);
  push_u16(atn, 1);
  push_u16(atn, 0);

  // 8. alt_label_map
  const uint32_t alt_label_off = static_cast<uint32_t>(atn.size());
  // alt 0 -> symbol 8, entry_state 2
  push_u16(atn, 0);
  push_u16(atn, 0);
  push_u16(atn, 8);
  push_u16(atn, 0);
  push_u32(atn, 2);
  // alt 1 -> symbol 9, entry_state 3
  push_u16(atn, 0);
  push_u16(atn, 1);
  push_u16(atn, 9);
  push_u16(atn, 0);
  push_u32(atn, 3);

  // 9. preds
  const uint32_t preds_off = static_cast<uint32_t>(atn.size());
  push_u32(atn, 0);  // name_off
  push_u32(atn, 5);  // name_len
  push_u32(atn, kPredPure | kPredHasDeps);
  push_u32(atn, 0);  // dep_start
  push_u32(atn, 1);  // dep_count

  // 10. dep_ids
  const uint32_t dep_ids_off = static_cast<uint32_t>(atn.size());
  push_u32(atn, 42);

  const uint32_t total_size = static_cast<uint32_t>(atn.size() + 8);
  write_u32_at(atn, off_table_pos + 0, strings_off);
  write_u32_at(atn, off_table_pos + 4, states_off);
  write_u32_at(atn, off_table_pos + 8, trans_off);
  write_u32_at(atn, off_table_pos + 12, sets_off);
  write_u32_at(atn, off_table_pos + 16, rules_off);
  write_u32_at(atn, off_table_pos + 20, decisions_off);
  write_u32_at(atn, off_table_pos + 24, field_map_off);
  write_u32_at(atn, off_table_pos + 28, alt_label_off);
  write_u32_at(atn, off_table_pos + 32, preds_off);
  write_u32_at(atn, off_table_pos + 36, dep_ids_off);
  write_u32_at(atn, off_table_pos + 40, total_size);

  const uint64_t atn_sum = lx::fnv1a_64(atn.data(), atn.size());
  push_u64(atn, atn_sum);

  return {std::move(atn), std::move(tbl)};
}

void test_valid_load_and_queries() {
  using namespace lx::atn;
  auto fix = build_sample_fixture();

  const lx_str pred_names[] = {{"is_ok", 5}};
  const lx_parser_tables pt = {
      sizeof(lx_parser_tables),
      LX_ATN_FORMAT_VERSION,
      fix.atn.data(),
      static_cast<uint32_t>(fix.atn.size()),
      fix.tables.data(),
      static_cast<uint32_t>(fix.tables.size()),
      0,
      1,
      pred_names,
  };

  std::string err;
  auto loaded = Atn::load(pt, &err);
  assert(loaded.has_value() && err.empty());

  const Atn& atn = *loaded;
  assert(atn.terminal_count() == 6);
  assert(atn.rules().size() == 2);
  assert(atn.states().size() == 13);
  assert(atn.transitions().size() == 13);
  assert(atn.decisions().size() == 3);

  // Set queries
  assert(atn.set_contains(0, LX_SYMBOL_EOF));
  assert(atn.set_contains(0, 4));
  assert(atn.set_contains(0, 5));
  assert(!atn.set_contains(0, 0));
  assert(!atn.set_contains(0, 2));

  // LL1 queries
  assert(atn.ll1_lookup(0, 4) == 0);
  assert(atn.ll1_lookup(0, 5) == 1);
  assert(atn.ll1_lookup(0, LX_SYMBOL_EOF) == 1);
  assert(atn.ll1_lookup(0, LX_SYMBOL_ERROR) == kTblError);

  // LLK queries
  assert(atn.llk_states(1).size() == 2);
  auto next_s = atn.llk_step(1, 0, 5);
  assert(next_s.has_value() && *next_s == 1);
  assert(atn.llk_states(1)[*next_s].accept_alt == 0);
  assert(!atn.llk_step(1, 0, 4).has_value());

  // Alt labels, fields, preds, sync sets
  assert(atn.resolve_alt_symbol(0, 2) == 8);
  assert(atn.resolve_alt_symbol(0, 3) == 9);
  assert(atn.resolve_alt_symbol(0, 999) == 6);
  assert(atn.rule_field_map(0).size() == 1);
  assert(atn.rule_field_map(0)[0].field_id == 1);
  assert(atn.pred_name(0) == "is_ok");
  assert(atn.pred_deps(0).size() == 1 && atn.pred_deps(0)[0] == 42);
  assert(atn.effective_sync_set(1) == 0);

  // Language info validation
  lx_symbol_info syms[10]{};
  const lx_str fields[2] = {{"", 0}, {"left", 4}};
  lx_language_info info{};
  info.size = sizeof(lx_language_info);
  info.symbols = syms;
  info.symbol_count = 10;
  info.fields = fields;
  info.field_count = 2;
  assert(atn.validate_against_info(info, pt, &err));
}

void test_corrupted_atn_rejected() {
  using namespace lx::atn;
  {
    auto fix = build_sample_fixture();
    fix.atn[24] ^= 0x01u;  // flip byte before checksum
    std::string err;
    auto res = Atn::load(fix.atn.data(), static_cast<uint32_t>(fix.atn.size()),
                         fix.tables.data(), static_cast<uint32_t>(fix.tables.size()), &err);
    assert(!res.has_value());
    assert(err.find("checksum") != std::string::npos);
  }
  {
    auto fix = build_sample_fixture();
    fix.tables[20] ^= 0x01u;  // corrupt LXTBL
    std::string err;
    auto res = Atn::load(fix.atn.data(), static_cast<uint32_t>(fix.atn.size()),
                         fix.tables.data(), static_cast<uint32_t>(fix.tables.size()), &err);
    assert(!res.has_value());
    assert(err.find("checksum") != std::string::npos);
  }
  {
    // Unreachable state check (break transition from state 0 -> 1 to 0 -> 6, then recompute checksum)
    auto fix = build_sample_fixture();
    const uint32_t trans_off =
        static_cast<uint32_t>(fix.atn[68]) |
        (static_cast<uint32_t>(fix.atn[69]) << 8) |
        (static_cast<uint32_t>(fix.atn[70]) << 16) |
        (static_cast<uint32_t>(fix.atn[71]) << 24);
    // trans[0].to is at trans_off + 4
    write_u32_at(fix.atn, trans_off + 4, 6);
    const uint64_t new_sum = lx::fnv1a_64(fix.atn.data(), fix.atn.size() - 8);
    write_u64_at(fix.atn, fix.atn.size() - 8, new_sum);

    std::string err;
    auto res = Atn::load(fix.atn.data(), static_cast<uint32_t>(fix.atn.size()),
                         fix.tables.data(), static_cast<uint32_t>(fix.tables.size()), &err);
    assert(!res.has_value());
    assert(err.find("unreachable") != std::string::npos);
  }
}

}  // namespace

int main() {
  test_valid_load_and_queries();
  test_corrupted_atn_rejected();
  return 0;
}
