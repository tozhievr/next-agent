/* IEEE binary32 and binary64 representation operations. */
#include <stdint.h>
#include <float.h>
#include <fenv.h>

#define DEFINE_IEC(S, T, U, SIGN, EXP, FRAC, QUIET, LIMIT)                    \
static U bits##S(const T *p) { U u; __builtin_memcpy(&u, p, sizeof u); return u; } \
static void put##S(T *p, U u) { __builtin_memcpy(p, &u, sizeof u); }            \
static int snan##S(U u) {                                                     \
    return (u & (EXP)) == (EXP) && (u & (FRAC)) && !(u & (QUIET));           \
}                                                                             \
int __libc_issignaling##S(T x) { return snan##S(bits##S(&x)); }               \
int __libc_iscanonical##S(T x) { (void)x; return 1; }                         \
int __libc_iseqsig##S(T x, T y) {                                            \
    U a = bits##S(&x), b = bits##S(&y);                                      \
    if (((a & (EXP)) == (EXP) && (a & (FRAC))) ||                            \
        ((b & (EXP)) == (EXP) && (b & (FRAC)))) {                           \
        feraiseexcept(FE_INVALID); return 0;                                 \
    }                                                                         \
    return x == y;                                                           \
}                                                                             \
int canonicalize##S(T *out, const T *x) {                                    \
    U u = bits##S(x);                                                         \
    if (snan##S(u)) { u |= (QUIET); feraiseexcept(FE_INVALID); }             \
    put##S(out, u); return 0;                                                 \
}                                                                             \
int totalorder##S(const T *x, const T *y) {                                  \
    U a = bits##S(x), b = bits##S(y);                                        \
    a = (a & (SIGN)) ? ~a : a | (SIGN);                                      \
    b = (b & (SIGN)) ? ~b : b | (SIGN);                                      \
    return a <= b;                                                           \
}                                                                             \
int totalordermag##S(const T *x, const T *y) {                               \
    return (bits##S(x) & ~(SIGN)) <= (bits##S(y) & ~(SIGN));                 \
}                                                                             \
T getpayload##S(const T *x) {                                                \
    U u = bits##S(x);                                                         \
    if ((u & (EXP)) != (EXP) || !(u & (FRAC))) return (T)-1;                 \
    return (T)(u & ((QUIET) - 1));                                           \
}                                                                             \
static int set##S(T *out, T payload, int sig) {                              \
    U u = bits##S(&payload), p;                                               \
    if (((u & (SIGN)) && (u & ~(SIGN))) || (u & (EXP)) == (EXP) ||          \
        payload >= (T)(LIMIT)) { put##S(out, 0); return 1; }                  \
    p = (U)payload;                                                          \
    if ((T)p != payload || (sig && !p)) { put##S(out, 0); return 1; }        \
    put##S(out, (EXP) | (sig ? 0 : (QUIET)) | p); return 0;                 \
}                                                                             \
int setpayload##S(T *out, T payload) { return set##S(out, payload, 0); }       \
int setpayloadsig##S(T *out, T payload) { return set##S(out, payload, 1); }

DEFINE_IEC(f, float, uint32_t, UINT32_C(0x80000000), UINT32_C(0x7f800000),
           UINT32_C(0x007fffff), UINT32_C(0x00400000), 0x1p22f)
DEFINE_IEC(, double, uint64_t, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000),
           UINT64_C(0x000fffffffffffff), UINT64_C(0x0008000000000000), 0x1p51)
#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
DEFINE_IEC(l, long double, uint64_t, UINT64_C(0x8000000000000000), UINT64_C(0x7ff0000000000000),
           UINT64_C(0x000fffffffffffff), UINT64_C(0x0008000000000000), 0x1p51L)
#endif
#if LDBL_MANT_DIG != 53 || LDBL_MAX_EXP != 1024
int __libc_iseqsigl(long double x, long double y) {
    if (__builtin_isnan(x) || __builtin_isnan(y)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    return x == y;
}
#endif
