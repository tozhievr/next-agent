#include "../interfaces/api.hpp"
#include <cassert>
#include <stdexcept>
int main() {
  auto t = lex("10 + 20*(3-1)");
  assert(t.size() == 9);
  assert((t[0] == Token{TokenKind::Number, "10", 0}));
  assert((t[1] == Token{TokenKind::Plus, "+", 3}));
  assert((t[3] == Token{TokenKind::Star, "*", 7}));
  assert((t[8] == Token{TokenKind::RParen, ")", 11}));
  assert(lex(" 	").empty());
  bool bad=false;
  try{
    (void)lex("1@2");
}catch(const std::invalid_argument&){bad=true;}assert(bad);}
