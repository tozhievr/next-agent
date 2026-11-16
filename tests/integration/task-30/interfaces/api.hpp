#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
enum class TokenKind { Number, Plus, Minus, Star, Slash, LParen, RParen };
struct Token { TokenKind kind; std::string lexeme; std::size_t offset; bool operator==(const Token&) const = default; };
std::vector<Token> lex(std::string_view source); // throw std::invalid_argument for invalid character
