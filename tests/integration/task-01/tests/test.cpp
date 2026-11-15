#include "../interfaces/api.hpp"
#include <cassert>
#include <limits>
int main(){using L=std::numeric_limits<std::int64_t>;assert(checked_add(2,3)==5);assert(checked_add(L::max(),1)==std::nullopt);assert(checked_add(L::min(),-1)==std::nullopt);assert(checked_mul(7,-6)==-42);assert(checked_mul(L::max(),2)==std::nullopt);assert(checked_mul(L::min(),-1)==std::nullopt);assert(checked_mul(L::min(),1)==L::min());}
