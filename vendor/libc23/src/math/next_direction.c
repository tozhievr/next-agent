#include "libm.h"
#include <fenv.h>
#include <limits.h>
#ifndef FP_LLOGB0
#define FP_LLOGB0 LONG_MIN
#define FP_LLOGBNAN LONG_MIN
#endif

#define STEP(T,U,S,EXP,Q,ASU,ASF) \
static T step##S(T x,int up) { \
    U u=ASU(x), mag=u & ~(U)(EXP##SIGN); \
    if (mag > (U)(EXP)) { if (!(u & (U)(Q))) { feraiseexcept(FE_INVALID); u |= (U)(Q); } return ASF(u); } \
    if (mag==0) return ASF(up ? (U)1 : (U)(EXP##SIGN)|1); \
    if ((up && u==(U)(EXP)) || (!up && u==((U)(EXP##SIGN)|(U)(EXP)))) return x; \
    if (((u & (U)(EXP##SIGN))==0)==up) ++u; else --u; \
    return ASF(u); \
}
#define DEXPSIGN 0x8000000000000000ULL
#define FEXPSIGN 0x80000000U
#define DEXP 0x7ff0000000000000ULL
#define FEXP 0x7f800000U
STEP(double,uint64_t,d,DEXP,0x0008000000000000ULL,asuint64,asdouble)
STEP(float,uint32_t,f,FEXP,0x00400000U,asuint,asfloat)
double nextup(double x) { return stepd(x,1); }
double nextdown(double x) { return stepd(x,0); }
float nextupf(float x) { return stepf(x,1); }
float nextdownf(float x) { return stepf(x,0); }

typedef union { long double f; struct { uint64_t lo, hi; } u; } ldrep;
static long double stepl(long double x,int up) {
#if LDBL_MANT_DIG == 53
    return (long double)stepd((double)x,up);
#elif LDBL_MANT_DIG == 113
    ldrep a={.f=x}; uint64_t h=a.u.hi, l=a.u.lo, m=h&0x7fffffffffffffffULL;
    if (m>0x7fff000000000000ULL || (m==0x7fff000000000000ULL && l)) {
        if (!(h & 0x0000800000000000ULL)) { feraiseexcept(FE_INVALID); a.u.hi|=0x0000800000000000ULL; } return a.f;
    }
    if (!(m|l)) { a.u.hi=up?0:0x8000000000000000ULL; a.u.lo=1; return a.f; }
    if (m==0x7fff000000000000ULL && l==0 && ((h>>63)==(unsigned)!up)) return x;
    if (((h>>63)==0)==up) { if (++a.u.lo==0) ++a.u.hi; }
    else { if (a.u.lo--==0) --a.u.hi; }
    return a.f;
#elif LDBL_MANT_DIG == 64
    ldrep a={.f=x}; uint64_t sig=a.u.lo; unsigned se=(unsigned)a.u.hi&0xffffU, e=se&0x7fffU, sign=se>>15;
    if (e==0x7fff && (sig & 0x7fffffffffffffffULL)) { if (!(sig & 0x4000000000000000ULL)) { feraiseexcept(FE_INVALID); a.u.lo|=0x4000000000000000ULL; } return a.f; }
    if (!e && !sig) { a.u.lo=1; a.u.hi=(a.u.hi & ~0xffffULL) | (up?0:0x8000); return a.f; }
    if (e==0x7fff && sign==(unsigned)!up) return x;
    if ((sign==0)==up) { ++sig; if (!sig) { ++e; sig=0x8000000000000000ULL; } else if (!e && sig==0x8000000000000000ULL) e=1; }
    else { if (sig==0x8000000000000000ULL && e) { --e; sig=e?0xffffffffffffffffULL:0x7fffffffffffffffULL; } else --sig; }
    a.u.lo=sig; a.u.hi=(a.u.hi & ~0xffffULL) | (sign<<15) | e; return a.f;
#else
#error Unsupported long double format
#endif
}
long double nextupl(long double x) { return stepl(x,1); }
long double nextdownl(long double x) { return stepl(x,0); }

double fdim(double x,double y) { return isnan(x)||isnan(y) ? x+y : x>y ? x-y : 0.0; }
float fdimf(float x,float y) { return isnan(x)||isnan(y) ? x+y : x>y ? x-y : 0.0f; }
long double fdiml(long double x,long double y) { return isnan(x)||isnan(y) ? x+y : x>y ? x-y : 0.0L; }
long llogb(double x) { if (x==0) { feraiseexcept(FE_INVALID); return FP_LLOGB0; } if (isnan(x)) { feraiseexcept(FE_INVALID); return FP_LLOGBNAN; } if (isinf(x)) { feraiseexcept(FE_INVALID); return LONG_MAX; } return ilogb(x); }
long llogbf(float x) { return llogb(x); }
long llogbl(long double x) {
    if (x==0) { feraiseexcept(FE_INVALID); return FP_LLOGB0; }
    if (isnan(x)) { feraiseexcept(FE_INVALID); return FP_LLOGBNAN; }
    if (isinf(x)) { feraiseexcept(FE_INVALID); return LONG_MAX; }
#if LDBL_MANT_DIG == 53
    return llogb((double)x);
#else
    ldrep a={.f=x}; unsigned e;
#if LDBL_MANT_DIG == 64
    e=(unsigned)a.u.hi & 0x7fffU;
    if (e) return (long)e-16383;
    return -16382L - __builtin_clzll(a.u.lo);
#else
    e=(unsigned)(a.u.hi>>48)&0x7fffU;
    if (e) return (long)e-16383;
    uint64_t hi=a.u.hi & 0x0000ffffffffffffULL;
    return hi ? -16383L-(__builtin_clzll(hi)-16) : -16431L-__builtin_clzll(a.u.lo);
#endif
#endif
}
