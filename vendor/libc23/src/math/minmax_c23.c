/* C23 IEEE 60559 maximum/minimum operations. */
#include <float.h>
#include <stdint.h>
#include <fenv.h>

/* NaN classification and quieting use representation bits to avoid raising
   invalid merely by inspecting a signaling operand. */
#define DEFINE_MINMAX(T, U, suffix, EXP, FRAC, QUIET, SIGN) \
static T select##suffix(T x, T y, int minimum, int magnitude, int number) { \
    union { T f; U u; } a = { .f = x }, b = { .f = y }, z; \
    int an = (a.u & (EXP)) == (EXP) && (a.u & (FRAC)); \
    int bn = (b.u & (EXP)) == (EXP) && (b.u & (FRAC)); \
    int signaling = (an && !(a.u & (QUIET))) || (bn && !(b.u & (QUIET))); \
    if (signaling) feraiseexcept(FE_INVALID); \
    if (an || bn) { \
        if (number && an != bn) return an ? y : x; \
        z = an ? a : b; z.u |= (QUIET); return z.f; \
    } \
    if (magnitude) { \
        U ax = a.u & ~(U)(SIGN), by = b.u & ~(U)(SIGN); \
        if (ax != by) return (minimum ? ax < by : ax > by) ? x : y; \
    } else { \
        if (x < y) return minimum ? x : y; \
        if (x > y) return minimum ? y : x; \
    } \
    if ((a.u ^ b.u) & (SIGN)) return ((a.u & (SIGN)) != 0) == minimum ? x : y; \
    return x; \
} \
T fmaximum##suffix(T x,T y) { return select##suffix(x,y,0,0,0); } \
T fminimum##suffix(T x,T y) { return select##suffix(x,y,1,0,0); } \
T fmaximum_num##suffix(T x,T y) { return select##suffix(x,y,0,0,1); } \
T fminimum_num##suffix(T x,T y) { return select##suffix(x,y,1,0,1); } \
T fmaximum_mag##suffix(T x,T y) { return select##suffix(x,y,0,1,0); } \
T fminimum_mag##suffix(T x,T y) { return select##suffix(x,y,1,1,0); } \
T fmaximum_mag_num##suffix(T x,T y) { return select##suffix(x,y,0,1,1); } \
T fminimum_mag_num##suffix(T x,T y) { return select##suffix(x,y,1,1,1); }
DEFINE_MINMAX(double,uint64_t,,0x7ff0000000000000ULL,0x000fffffffffffffULL,0x0008000000000000ULL,0x8000000000000000ULL)
DEFINE_MINMAX(float,uint32_t,f,0x7f800000U,0x007fffffU,0x00400000U,0x80000000U)

/* Supported targets are little-endian: binary64 on Windows, x87 extended on
   x86-64 ELF, and binary128 on AArch64 ELF. */
typedef union { long double f; unsigned char b[sizeof(long double)]; } ld_bits;
static int ld_sign(ld_bits a) {
#if LDBL_MANT_DIG == 53
    return a.b[7] >> 7;
#else
    return a.b[sizeof(long double) == 16 && LDBL_MANT_DIG == 113 ? 15 : 9] >> 7;
#endif
}
static int ld_nan(ld_bits a) {
#if LDBL_MANT_DIG == 53
    uint64_t u; __builtin_memcpy(&u, a.b, sizeof u);
    return (u & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL && (u & 0xfffffffffffffULL);
#elif LDBL_MANT_DIG == 64
    uint64_t m; __builtin_memcpy(&m, a.b, sizeof m);
    return (a.b[9] & 0x7f) == 0x7f && a.b[8] == 0xff && (m & 0x7fffffffffffffffULL);
#elif LDBL_MANT_DIG == 113
    if ((a.b[15] & 0x7f) != 0x7f || a.b[14] != 0xff) return 0;
    for (int i=0; i<14; ++i) if (a.b[i]) return 1;
    return 0;
#else
#error Unsupported long double format
#endif
}
#if LDBL_MANT_DIG == 53
#define LD_QUIET_BYTE 6
#define LD_QUIET_MASK 0x08
#elif LDBL_MANT_DIG == 64
#define LD_QUIET_BYTE 7
#define LD_QUIET_MASK 0x40
#else
#define LD_QUIET_BYTE 13
#define LD_QUIET_MASK 0x80
#endif
static long double selectl(long double x, long double y, int minimum, int magnitude, int number) {
    ld_bits a = {.f=x}, b = {.f=y}, z;
    int an=ld_nan(a), bn=ld_nan(b), sx=ld_sign(a), sy=ld_sign(b);
    if ((an && !(a.b[LD_QUIET_BYTE] & LD_QUIET_MASK)) || (bn && !(b.b[LD_QUIET_BYTE] & LD_QUIET_MASK))) feraiseexcept(FE_INVALID);
    if (an || bn) {
        if (number && an != bn) return an ? y : x;
        z = an ? a : b; z.b[LD_QUIET_BYTE] |= LD_QUIET_MASK; return z.f;
    }
    if (magnitude) {
        long double ax = sx ? -x : x, by = sy ? -y : y;
        if (ax != by) return (minimum ? ax < by : ax > by) ? x : y;
    } else {
        if (x < y) return minimum ? x : y;
        if (x > y) return minimum ? y : x;
    }
    if (sx != sy) return sx == minimum ? x : y;
    return x;
}
#define LD_FN(name, min, mag, num) long double name##l(long double x,long double y) { return selectl(x,y,min,mag,num); }
LD_FN(fmaximum,0,0,0) LD_FN(fminimum,1,0,0)
LD_FN(fmaximum_num,0,0,1) LD_FN(fminimum_num,1,0,1)
LD_FN(fmaximum_mag,0,1,0) LD_FN(fminimum_mag,1,1,0)
LD_FN(fmaximum_mag_num,0,1,1) LD_FN(fminimum_mag_num,1,1,1)
