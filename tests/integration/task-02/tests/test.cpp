#include "../interfaces/api.hpp"
#include <cassert>
int main(){assert(utf8_count("")==0);assert(utf8_count("abc")==3);assert(utf8_count("Ã©")==1);assert(utf8_count("ð")==1);assert(!utf8_count("À¯"));assert(!utf8_count("í "));assert(!utf8_count("ô"));assert(!utf8_count("â"));}
