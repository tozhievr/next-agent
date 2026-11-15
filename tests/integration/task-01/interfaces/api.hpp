#pragma once
#include <cstdint>
#include <optional>
std::optional<std::int64_t> checked_add(std::int64_t a, std::int64_t b);
std::optional<std::int64_t> checked_mul(std::int64_t a, std::int64_t b);
