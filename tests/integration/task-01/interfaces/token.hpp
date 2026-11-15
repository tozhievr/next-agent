#pragma once

#include <concepts>
#include <string>
#include <variant>
#include <format>

template <class T>
concept StaticTokenContract = requires(const T& ctk) {
    { ctk.repr() } -> std::same_as<std::string>;
};

enum class TokenType {
    PLUS = 1,
    MINUS = 2,
    STAR = 3,
    SLASH = 4,
    LPAR = 5,
    RPAR = 6,

    UNKNOWN = 0
};

using ValueType = std::variant<std::monostate, int, double, char, std::string>;

struct MockToken {
    TokenType type;
    std::string lexeme;
    ValueType value;
    uint32_t row;
    uint32_t col;

    std::string repr() const {
        return std::format("MockToken(type: {}, lexeme: '{}', row: {}, col: {})", 
            static_cast<int>(type), lexeme,
            row, col  
        );
    }
};

static_assert(StaticTokenContract<MockToken> && "MockToken doesn't satisfy StaticTokenContract!");