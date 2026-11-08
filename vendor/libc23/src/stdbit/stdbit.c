/* Exported stdc_* functions for the five standard unsigned integer types.
 * The type-generic macros use Clang's C23 builtins so they also accept
 * extended and bit-precise unsigned integer types. */
#include <stdbit.h>

#define DEFS(T, S) \
    unsigned int stdc_leading_zeros_##S(T v)       { return __stdbit_lz_##S(v); } \
    unsigned int stdc_leading_ones_##S(T v)        { return __stdbit_lo_##S(v); } \
    unsigned int stdc_trailing_zeros_##S(T v)      { return __stdbit_tz_##S(v); } \
    unsigned int stdc_trailing_ones_##S(T v)       { return __stdbit_to_##S(v); } \
    unsigned int stdc_first_leading_zero_##S(T v)  { return __stdbit_fz_##S(v); } \
    unsigned int stdc_first_leading_one_##S(T v)   { return __stdbit_fl_##S(v); } \
    unsigned int stdc_first_trailing_zero_##S(T v) { return __stdbit_ftz_##S(v); } \
    unsigned int stdc_first_trailing_one_##S(T v)  { return __stdbit_ft_##S(v); } \
    unsigned int stdc_count_zeros_##S(T v)         { return __stdbit_cz_##S(v); } \
    unsigned int stdc_count_ones_##S(T v)          { return __stdbit_co_##S(v); } \
    bool         stdc_has_single_bit_##S(T v)      { return __stdbit_hs_##S(v); } \
    unsigned int stdc_bit_width_##S(T v)           { return __stdbit_bw_##S(v); } \
    T            stdc_bit_floor_##S(T v)           { return __stdbit_bf_##S(v); } \
    T            stdc_bit_ceil_##S(T v)            { return __stdbit_bc_##S(v); }

DEFS(unsigned char, uc)
DEFS(unsigned short, us)
DEFS(unsigned int, ui)
DEFS(unsigned long, ul)
DEFS(unsigned long long, ull)
