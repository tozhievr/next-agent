/* <stdbit.h> - C23 7.18 (libc23) */
#ifndef _LIBC23_STDBIT_H
#define _LIBC23_STDBIT_H

#include <stdbool.h>

#define __STDC_VERSION_STDBIT_H__ 202311L

#define __STDC_ENDIAN_LITTLE__ __ORDER_LITTLE_ENDIAN__
#define __STDC_ENDIAN_BIG__    __ORDER_BIG_ENDIAN__
#define __STDC_ENDIAN_NATIVE__ __BYTE_ORDER__

#ifdef __cplusplus
extern "C" {
#endif

/* ---- exported functions (one set per standard unsigned integer type) ---- */
#define __STDBIT_DECLS(T, S) \
    unsigned int stdc_leading_zeros_##S(T); \
    unsigned int stdc_leading_ones_##S(T); \
    unsigned int stdc_trailing_zeros_##S(T); \
    unsigned int stdc_trailing_ones_##S(T); \
    unsigned int stdc_first_leading_zero_##S(T); \
    unsigned int stdc_first_leading_one_##S(T); \
    unsigned int stdc_first_trailing_zero_##S(T); \
    unsigned int stdc_first_trailing_one_##S(T); \
    unsigned int stdc_count_zeros_##S(T); \
    unsigned int stdc_count_ones_##S(T); \
    bool stdc_has_single_bit_##S(T); \
    unsigned int stdc_bit_width_##S(T); \
    T stdc_bit_floor_##S(T); \
    T stdc_bit_ceil_##S(T);

__STDBIT_DECLS(unsigned char, uc)
__STDBIT_DECLS(unsigned short, us)
__STDBIT_DECLS(unsigned int, ui)
__STDBIT_DECLS(unsigned long, ul)
__STDBIT_DECLS(unsigned long long, ull)

/* ---- inline implementations used by the generic macros ------------------ */
#define __STDBIT_INLINES(T, S) \
    static inline unsigned int __stdbit_lz_##S(T v) { \
        return v ? (unsigned)__builtin_clzll((unsigned long long)v) - (64u - (unsigned)(sizeof(T) * 8)) \
                 : (unsigned)(sizeof(T) * 8); } \
    static inline unsigned int __stdbit_tz_##S(T v) { \
        return v ? (unsigned)__builtin_ctzll((unsigned long long)v) : (unsigned)(sizeof(T) * 8); } \
    static inline unsigned int __stdbit_lo_##S(T v) { return __stdbit_lz_##S((T)~v); } \
    static inline unsigned int __stdbit_to_##S(T v) { return __stdbit_tz_##S((T)~v); } \
    static inline unsigned int __stdbit_fz_##S(T v) { \
        return (T)~v ? __stdbit_lz_##S((T)~v) + 1u : 0u; } \
    static inline unsigned int __stdbit_fl_##S(T v) { return v ? __stdbit_lz_##S(v) + 1u : 0u; } \
    static inline unsigned int __stdbit_ftz_##S(T v) { \
        return (T)~v ? __stdbit_tz_##S((T)~v) + 1u : 0u; } \
    static inline unsigned int __stdbit_ft_##S(T v) { return v ? __stdbit_tz_##S(v) + 1u : 0u; } \
    static inline unsigned int __stdbit_co_##S(T v) { return (unsigned)__builtin_popcountll((unsigned long long)v); } \
    static inline unsigned int __stdbit_cz_##S(T v) { return (unsigned)(sizeof(T) * 8) - __stdbit_co_##S(v); } \
    static inline bool __stdbit_hs_##S(T v) { return __stdbit_co_##S(v) == 1u; } \
    static inline unsigned int __stdbit_bw_##S(T v) { return (unsigned)(sizeof(T) * 8) - __stdbit_lz_##S(v); } \
    static inline T __stdbit_bf_##S(T v) { return v ? (T)((T)1 << (__stdbit_bw_##S(v) - 1u)) : (T)0; } \
    static inline T __stdbit_bc_##S(T v) { \
        unsigned int width = __stdbit_bw_##S((T)(v - 1)); \
        return v <= 1 ? (T)1 : width >= (unsigned)(sizeof(T) * 8) ? (T)0 : (T)((unsigned long long)1 << width); }

__STDBIT_INLINES(unsigned char, uc)
__STDBIT_INLINES(unsigned short, us)
__STDBIT_INLINES(unsigned int, ui)
__STDBIT_INLINES(unsigned long, ul)
__STDBIT_INLINES(unsigned long long, ull)

#ifdef __cplusplus
}
#endif

/* These Clang C23 builtins preserve the argument type for bit_floor/ceil,
 * return an unsigned int for counts and indices, accept extended and
 * bit-precise unsigned types without narrowing, and handle zero inputs.  Each
 * builtin evaluates its argument exactly once. */
#define stdc_leading_zeros(v)        __builtin_stdc_leading_zeros(v)
#define stdc_leading_ones(v)         __builtin_stdc_leading_ones(v)
#define stdc_trailing_zeros(v)       __builtin_stdc_trailing_zeros(v)
#define stdc_trailing_ones(v)        __builtin_stdc_trailing_ones(v)
#define stdc_first_leading_zero(v)   __builtin_stdc_first_leading_zero(v)
#define stdc_first_leading_one(v)    __builtin_stdc_first_leading_one(v)
#define stdc_first_trailing_zero(v)  __builtin_stdc_first_trailing_zero(v)
#define stdc_first_trailing_one(v)   __builtin_stdc_first_trailing_one(v)
#define stdc_count_zeros(v)          __builtin_stdc_count_zeros(v)
#define stdc_count_ones(v)           __builtin_stdc_count_ones(v)
#define stdc_has_single_bit(v)       __builtin_stdc_has_single_bit(v)
#define stdc_bit_width(v)            __builtin_stdc_bit_width(v)
#define stdc_bit_floor(v)            __builtin_stdc_bit_floor(v)
#define stdc_bit_ceil(v)             __builtin_stdc_bit_ceil(v)

#endif
