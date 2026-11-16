#pragma once
#include <optional>
#include <string_view>
#include <cstddef>
std::optional<std::size_t> utf8_count(std::string_view input);
