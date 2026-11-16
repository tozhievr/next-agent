#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lx {

inline constexpr uint64_t kFnv1a64OffsetBasis = 0xCBF29CE484222325ULL;
inline constexpr uint64_t kFnv1a64Prime = 0x100000001B3ULL;

inline constexpr uint32_t kFnv1a32OffsetBasis = 0x811C9DC5u;
inline constexpr uint32_t kFnv1a32Prime = 0x01000193u;

[[nodiscard]] constexpr uint64_t fnv1a_64(
    const uint8_t* data,
    size_t len,
    uint64_t seed = kFnv1a64OffsetBasis) noexcept {
  uint64_t h = seed;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint64_t>(data[i]);
    h *= kFnv1a64Prime;
  }
  return h;
}

[[nodiscard]] inline uint64_t fnv1a_64(
    const void* data,
    size_t len,
    uint64_t seed = kFnv1a64OffsetBasis) noexcept {
  return fnv1a_64(static_cast<const uint8_t*>(data), len, seed);
}

[[nodiscard]] constexpr uint64_t fnv1a_64(
    std::string_view sv,
    uint64_t seed = kFnv1a64OffsetBasis) noexcept {
  uint64_t h = seed;
  for (char ch : sv) {
    h ^= static_cast<uint8_t>(ch);
    h *= kFnv1a64Prime;
  }
  return h;
}

[[nodiscard]] constexpr uint32_t fnv1a_32(
    const uint8_t* data,
    size_t len,
    uint32_t seed = kFnv1a32OffsetBasis) noexcept {
  uint32_t h = seed;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint32_t>(data[i]);
    h *= kFnv1a32Prime;
  }
  return h;
}

[[nodiscard]] inline uint32_t fnv1a_32(
    const void* data,
    size_t len,
    uint32_t seed = kFnv1a32OffsetBasis) noexcept {
  return fnv1a_32(static_cast<const uint8_t*>(data), len, seed);
}

[[nodiscard]] constexpr uint32_t fnv1a_32(
    std::string_view sv,
    uint32_t seed = kFnv1a32OffsetBasis) noexcept {
  uint32_t h = seed;
  for (char ch : sv) {
    h ^= static_cast<uint8_t>(ch);
    h *= kFnv1a32Prime;
  }
  return h;
}

[[nodiscard]] constexpr uint64_t mix64(uint64_t x) noexcept {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

[[nodiscard]] constexpr uint32_t mix32(uint32_t x) noexcept {
  x ^= x >> 16;
  x *= 0x85ebca6bu;
  x ^= x >> 13;
  x *= 0xc2b2ae35u;
  x ^= x >> 16;
  return x;
}

[[nodiscard]] constexpr uint64_t hash_combine_64(uint64_t h, uint64_t v) noexcept {
  h ^= mix64(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

[[nodiscard]] constexpr uint32_t hash_combine_32(uint32_t h, uint32_t v) noexcept {
  h ^= mix32(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
  return h;
}

}  // namespace lx
