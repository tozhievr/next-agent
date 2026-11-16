#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lx {

struct LexState {
  uint32_t mode_stack_id = 0;
  uint64_t ext_id = 0;

  bool operator==(const LexState&) const noexcept = default;
  bool operator!=(const LexState&) const noexcept = default;
};

// Thread-safe interner for persistent lexer mode stacks, external state blobs
// (backing lx_host_api.state_intern / state_resolve), and combined 32-bit
// LexState IDs stored in GreenToken::lex_state_in / lex_state_out.
class StateInterner {
 public:
  // Command codes matching lx_language.h (LX_CMD_NONE..LX_CMD_MODE).
  static constexpr uint8_t kCmdNone = 0;
  static constexpr uint8_t kCmdPush = 1;
  static constexpr uint8_t kCmdPop = 2;
  static constexpr uint8_t kCmdMode = 3;

  StateInterner();
  ~StateInterner() = default;

  StateInterner(const StateInterner&) = delete;
  StateInterner& operator=(const StateInterner&) = delete;

  // --- Mode stack ---
  uint16_t top_mode(uint32_t mode_stack_id) const noexcept;
  uint32_t push_mode(uint32_t mode_stack_id, uint16_t mode);
  uint32_t pop_mode(uint32_t mode_stack_id) const noexcept;
  uint32_t replace_mode(uint32_t mode_stack_id, uint16_t mode);
  uint32_t apply_mode_cmd(uint32_t mode_stack_id, uint8_t cmd, uint16_t cmd_arg);

  // --- External state blob (0 == initial empty state) ---
  uint64_t intern_ext(const void* data, uint32_t len);
  uint64_t intern_ext(std::string_view blob) {
    return intern_ext(blob.data(), static_cast<uint32_t>(blob.size()));
  }
  // Returned view remains valid for the lifetime of this StateInterner.
  std::string_view resolve_ext(uint64_t id) const noexcept;

  // --- Combined LexState <-> uint32_t state_id (0 == {0, 0}) ---
  uint32_t intern_state(LexState state);
  LexState resolve_state(uint32_t state_id) const noexcept;
  uint32_t apply(uint32_t state_id, uint8_t cmd, uint16_t cmd_arg, uint64_t ext_next);

  // Diagnostics / testing
  uint32_t mode_stack_count() const noexcept;
  uint64_t ext_count() const noexcept;
  uint32_t state_count() const noexcept;

 private:
  struct ModeNode {
    uint32_t parent;
    uint16_t mode;
  };

  struct ExtEntry {
    std::unique_ptr<char[]> data;
    uint32_t len = 0;
  };

  struct LexStateHash {
    size_t operator()(const LexState& s) const noexcept;
  };

  struct StringViewHash {
    size_t operator()(std::string_view sv) const noexcept;
  };

  uint32_t intern_mode_node(uint32_t parent, uint16_t mode);

  mutable std::shared_mutex mode_mu_;
  std::vector<ModeNode> mode_nodes_;
  std::unordered_map<uint64_t, uint32_t> mode_map_;

  mutable std::shared_mutex ext_mu_;
  std::vector<ExtEntry> ext_entries_;
  std::unordered_map<std::string_view, uint64_t, StringViewHash> ext_map_;

  mutable std::shared_mutex state_mu_;
  std::vector<LexState> states_;
  std::unordered_map<LexState, uint32_t, LexStateHash> state_map_;
};

}  // namespace lx
