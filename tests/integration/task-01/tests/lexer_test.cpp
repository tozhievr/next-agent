#include <concepts>
#include <format>

#include <iostream>

#include "../interfaces/lexer.hpp"

template <class T>
  requires(std::equality_comparable<T>)
static auto eq_assert = [](T a, T b) {
  if (a != b)
    throw std::exception("ASSERTATION_FAILED");
};

bool test_01(ILexer *lexer) try {
  auto tokens = lexer->run("10 + 20");
  eq_assert<size_t>(tokens.size(), static_cast<size_t>(3));
  eq_assert<std::string>(tokens[0].lexeme, "10");
  eq_assert<std::string>(tokens[1].lexeme, "+");  
  eq_assert<std::string>(tokens[2].lexeme, "20");
  return true;  
} catch (std::exception &e) {
  std::cerr << std::format("C++ Standard Exception: {}\n", e.what());
  return false;
}