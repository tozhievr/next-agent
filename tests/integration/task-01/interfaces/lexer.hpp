#pragma once

#include <concepts>
#include <vector>
#include <string>

#include "token.hpp"

class ILexer {
    public:
    virtual ~ILexer() noexcept = default;

    virtual std::vector<MockToken> run(const std::string& source) = 0;
};

template <class T>
concept StaticLexerContract = requires(T lex, const std::string& source) {
    std::constructible_from<T>;

    { lex.run(source) } -> std::same_as<std::vector<MockToken>>;
};

static_assert(StaticLexerContract<ILexer>, "ILexer doesn't satisfy StaticLexerContract!");
