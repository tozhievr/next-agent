#include "interfaces/api.hpp"
std::optional<std::int64_t> checked_add(std::int64_t a,std::int64_t b){std::int64_t r;if(__builtin_add_overflow(a,b,&r))return {};return r;}
std::optional<std::int64_t> checked_mul(std::int64_t a,std::int64_t b){std::int64_t r;if(__builtin_mul_overflow(a,b,&r))return {};return r;}
