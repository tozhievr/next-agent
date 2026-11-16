#include "state_interner.h"

#include <cstring>
#include <limits>
#include <mutex>

namespace lx {

namespace {

constexpr uint32_t kRootParent = std::numeric_limits<uint32_t>::max();

inline uint64_t pack_mode_key(uint32_t parent, uint16_t mode) noexcept {
  return (static_cast<uint64_t>(parent) << 16) | static_cast<uint64_t>(mode);
}

inline uint64_t fnv1a_64(const char* data, size_t len) noexcept {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(data[i]);
    h *= 1099511628211ULL;
  }
  return h;
}

inline uint64_t splitmix64(uint64_t x) noexcept {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

}  // namespace

size_t StateInterner::LexStateHash::operator()(const LexState& s) const noexcept {
  uint64_t h = splitmix64(s.mode_stack_id);
  h ^= splitmix64(s.ext_id + 0x9e3779b97f4a7c15ULL);
  return static_cast<size_t>(h);
}

size_t StateInterner::StringViewHash::operator()(std::string_view sv) const noexcept {
  return static_cast<size_t>(fnv1a_64(sv.data(), sv.size()));
}

StateInterner::StateInterner() {
  mode_nodes_.push_back(ModeNode{kRootParent, 0});
  ext_entries_.push_back(ExtEntry{nullptr, 0});
  states_.push_back(LexState{0, 0});
}

uint16_t StateInterner::top_mode(uint32_t mode_stack_id) const noexcept {
  if (mode_stack_id == 0) return 0;
  std::shared_lock lock(mode_mu_);
  if (mode_stack_id >= mode_nodes_.size()) return 0;
  return mode_nodes_[mode_stack_id].mode;
}

uint32_t StateInterner::intern_mode_node(uint32_t parent, uint16_t mode) {
  if (parent == kRootParent && mode == 0) return 0;
  const uint64_t key = pack_mode_key(parent, mode);
  {
    std::shared_lock lock(mode_mu_);
    auto it = mode_map_.find(key);
    if (it != mode_map_.end()) return it->second;
  }
  std::unique_lock lock(mode_mu_);
  auto [it, inserted] = mode_map_.emplace(key, static_cast<uint32_t>(mode_nodes_.size()));
  if (inserted) {
    mode_nodes_.push_back(ModeNode{parent, mode});
  }
  return it->second;
}

uint32_t StateInterner::push_mode(uint32_t mode_stack_id, uint16_t mode) {
  {
    std::shared_lock lock(mode_mu_);
    if (mode_stack_id >= mode_nodes_.size()) mode_stack_id = 0;
  }
  return intern_mode_node(mode_stack_id, mode);
}

uint32_t StateInterner::pop_mode(uint32_t mode_stack_id) const noexcept {
  if (mode_stack_id == 0) return 0;
  std::shared_lock lock(mode_mu_);
  if (mode_stack_id >= mode_nodes_.size()) return 0;
  const uint32_t parent = mode_nodes_[mode_stack_id].parent;
  return (parent == kRootParent) ? 0u : parent;
}

uint32_t StateInterner::replace_mode(uint32_t mode_stack_id, uint16_t mode) {
  uint32_t parent = kRootParent;
  if (mode_stack_id != 0) {
    std::shared_lock lock(mode_mu_);
    if (mode_stack_id < mode_nodes_.size()) {
      parent = mode_nodes_[mode_stack_id].parent;
    }
  }
  return intern_mode_node(parent, mode);
}

uint32_t StateInterner::apply_mode_cmd(uint32_t mode_stack_id, uint8_t cmd, uint16_t cmd_arg) {
  switch (cmd) {
    case kCmdNone:
      return mode_stack_id;
    case kCmdPush:
      return push_mode(mode_stack_id, cmd_arg);
    case kCmdPop:
      return pop_mode(mode_stack_id);
    case kCmdMode:
      return replace_mode(mode_stack_id, cmd_arg);
    default:
      return mode_stack_id;
  }
}

uint64_t StateInterner::intern_ext(const void* data, uint32_t len) {
  if (len == 0 || data == nullptr) return 0;
  const std::string_view sv(static_cast<const char*>(data), len);
  {
    std::shared_lock lock(ext_mu_);
    auto it = ext_map_.find(sv);
    if (it != ext_map_.end()) return it->second;
  }
  std::unique_lock lock(ext_mu_);
  auto it = ext_map_.find(sv);
  if (it != ext_map_.end()) return it->second;

  auto buf = std::make_unique<char[]>(len);
  std::memcpy(buf.get(), data, len);
  const std::string_view owned_sv(buf.get(), len);
  const uint64_t id = static_cast<uint64_t>(ext_entries_.size());
  ext_entries_.push_back(ExtEntry{std::move(buf), len});
  ext_map_.emplace(owned_sv, id);
  return id;
}

std::string_view StateInterner::resolve_ext(uint64_t id) const noexcept {
  if (id == 0) return {};
  std::shared_lock lock(ext_mu_);
  if (id >= ext_entries_.size()) return {};
  const ExtEntry& e = ext_entries_[id];
  return std::string_view(e.data.get(), e.len);
}

uint32_t StateInterner::intern_state(LexState state) {
  if (state.mode_stack_id == 0 && state.ext_id == 0) return 0;
  {
    std::shared_lock lock(state_mu_);
    auto it = state_map_.find(state);
    if (it != state_map_.end()) return it->second;
  }
  std::unique_lock lock(state_mu_);
  auto [it, inserted] = state_map_.emplace(state, static_cast<uint32_t>(states_.size()));
  if (inserted) {
    states_.push_back(state);
  }
  return it->second;
}

LexState StateInterner::resolve_state(uint32_t state_id) const noexcept {
  if (state_id == 0) return LexState{0, 0};
  std::shared_lock lock(state_mu_);
  if (state_id >= states_.size()) return LexState{0, 0};
  return states_[state_id];
}

uint32_t StateInterner::apply(uint32_t state_id, uint8_t cmd, uint16_t cmd_arg,
                              uint64_t ext_next) {
  const LexState cur = resolve_state(state_id);
  const uint32_t next_mode = apply_mode_cmd(cur.mode_stack_id, cmd, cmd_arg);
  return intern_state(LexState{next_mode, ext_next});
}

uint32_t StateInterner::mode_stack_count() const noexcept {
  std::shared_lock lock(mode_mu_);
  return static_cast<uint32_t>(mode_nodes_.size());
}

uint64_t StateInterner::ext_count() const noexcept {
  std::shared_lock lock(ext_mu_);
  return static_cast<uint64_t>(ext_entries_.size());
}

uint32_t StateInterner::state_count() const noexcept {
  std::shared_lock lock(state_mu_);
  return static_cast<uint32_t>(states_.size());
}

}  // namespace lx
