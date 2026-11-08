/* Elementary IEEE binary64 operations. Long double entry points use double precision. */
#include "libm.h"
#include <fenv.h>

double fabs(double x) { return asdouble(asuint64(x) & 0x7fffffffffffffffULL); }
double copysign(double x, double y) { return asdouble((asuint64(x) & 0x7fffffffffffffffULL) | (asuint64(y) & 0x8000000000000000ULL)); }
double sqrt(double x) {
    double result;
#if defined(__x86_64__)
    __asm__("sqrtsd %1, %0" : "=x"(result) : "x"(x));
#elif defined(__aarch64__)
    __asm__("fsqrt %d0, %d1" : "=w"(result) : "w"(x));
#else
#error "sqrt requires an implementation for this architecture"
#endif
    return result;
}
double trunc(double x) {
    uint64_t u = asuint64(x); int e = (int)((u >> 52) & 0x7ff) - 1023;
    if (e < 0) return asdouble(u & 0x8000000000000000ULL);
    if (e >= 52) return e == 1024 && (u & 0x000fffffffffffffULL) ? x + x : x;
    return asdouble(u & ~(0x000fffffffffffffULL >> e));
}
double floor(double x) { double t = trunc(x); return x < t ? t - 1.0 : t; }
double ceil(double x) { double t = trunc(x); return x > t ? t + 1.0 : t; }
double round(double x) {
    if (!isfinite(x)) return isnan(x) ? x + x : x;
    double t = trunc(x), d = fabs(x - t);
    return d >= 0.5 ? t + copysign(1.0, x) : t;
}
static double minmax(double x, double y, int minimum) {
    uint64_t a=asuint64(x), b=asuint64(y);
    uint64_t ax=a & 0x7fffffffffffffffULL, by=b & 0x7fffffffffffffffULL;
    int an=ax>0x7ff0000000000000ULL, bn=by>0x7ff0000000000000ULL;
    if ((an && !(a & 0x0008000000000000ULL)) || (bn && !(b & 0x0008000000000000ULL))) feraiseexcept(FE_INVALID);
    if (an && bn) return asdouble(a | 0x0008000000000000ULL);
    if (an) return y;
    if (bn) return x;
    if (x == y) return (signbit(x) != 0) == minimum ? x : y;
    return (minimum ? x < y : x > y) ? x : y;
}
double fmax(double x, double y) { return minmax(x,y,0); }
double fmin(double x, double y) { return minmax(x,y,1); }
double modf(double x, double *ip) { *ip = trunc(x); if (isinf(x)) return copysign(0.0, x); return x == *ip ? copysign(0.0, x) : x - *ip; }
double frexp(double x, int *ep) {
    uint64_t u = asuint64(x), a = u & 0x7fffffffffffffffULL; int e = (int)(a >> 52);
    *ep = 0; if (e == 0x7ff) return (a & 0x000fffffffffffffULL) ? x + x : x; if (!a) return x;
    if (!e) { x *= 0x1p54; u = asuint64(x); e = (int)((u >> 52) & 0x7ff) - 54; }
    *ep = e - 1022; return asdouble((u & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL);
}
double ldexp(double x, int n) {
    if (!isfinite(x)) return isnan(x) ? x + x : x;
    if (x == 0.0) return x;
    /* Keep the final multiplication responsible for any subnormal rounding. */
    if (n > 1023) {
        x *= 0x1p1023; n -= 1023;
        if (n > 1023) { x *= 0x1p1023; n -= 1023; if (n > 1023) n = 1023; }
    } else if (n < -1022) {
        x *= 0x1p-969; n += 969;
        if (n < -1022) { x *= 0x1p-969; n += 969; if (n < -1022) n = -1022; }
    }
    return x * asdouble((uint64_t)(n + 1023) << 52);
}
double fmod(double x, double y) {
    uint64_t ux = asuint64(x), uy = asuint64(y), sx = ux & 0x8000000000000000ULL;
    int ex = (int)((ux >> 52) & 0x7ff), ey = (int)((uy >> 52) & 0x7ff);
    ux &= 0x7fffffffffffffffULL; uy &= 0x7fffffffffffffffULL;
    if (!uy || ex == 0x7ff || uy > 0x7ff0000000000000ULL) return __math_invalid(x * y);
    if (ux < uy) return x;
    if (ux == uy) return asdouble(sx);
    if (!ex) { for (uint64_t i = ux << 12; !(i >> 63); i <<= 1) ex--; ux <<= -ex + 1; }
    else { ux &= 0x000fffffffffffffULL; ux |= 0x0010000000000000ULL; }
    if (!ey) { for (uint64_t i = uy << 12; !(i >> 63); i <<= 1) ey--; uy <<= -ey + 1; }
    else { uy &= 0x000fffffffffffffULL; uy |= 0x0010000000000000ULL; }
    for (; ex > ey; ex--) { if (ux >= uy) { ux -= uy; if (!ux) return asdouble(sx); } ux <<= 1; }
    if (ux >= uy) ux -= uy;
    if (!ux) return asdouble(sx);
    while (!(ux & 0x0010000000000000ULL)) { ux <<= 1; ex--; }
    if (ex > 0) ux = (ux - 0x0010000000000000ULL) | ((uint64_t)ex << 52);
    else ux >>= 1 - ex;
    return asdouble(ux | sx);
}
#define UNARY(n) float n##f(float x) { return (float)n(x); } long double n##l(long double x) { return (long double)n((double)x); }
#define BINARY(n) float n##f(float x,float y) { return (float)n(x,y); } long double n##l(long double x,long double y) { return (long double)n((double)x,(double)y); }
UNARY(sqrt) UNARY(trunc) UNARY(floor) UNARY(ceil) UNARY(round)
BINARY(fmod) BINARY(fmax) BINARY(fmin)
float fabsf(float x) { return asfloat(asuint(x) & 0x7fffffffU); }
float copysignf(float x,float y) { return asfloat((asuint(x) & 0x7fffffffU) | (asuint(y) & 0x80000000U)); }
static long double ld_copysign(long double x,long double y) {
    union { long double f; unsigned char b[sizeof(long double)]; } a={.f=x}, b={.f=y};
#if LDBL_MANT_DIG == 53
    const int sign_byte=7;
#elif LDBL_MANT_DIG == 64
    const int sign_byte=9;
#else
    const int sign_byte=15;
#endif
    a.b[sign_byte]=(a.b[sign_byte]&0x7f) | (b.b[sign_byte]&0x80);
    return a.f;
}
long double copysignl(long double x,long double y) { return ld_copysign(x,y); }
long double fabsl(long double x) { return ld_copysign(x,0.0L); }
float modff(float x, float *p) { double i; double r=modf(x,&i); *p=(float)i; return (float)r; }
long double modfl(long double x, long double *p) { double i; double r=modf((double)x,&i); *p=(long double)i; return (long double)r; }
float frexpf(float x, int *p) { return (float)frexp(x,p); }
long double frexpl(long double x, int *p) { return (long double)frexp((double)x,p); }
float ldexpf(float x, int n) { return (float)ldexp(x,n); }
long double ldexpl(long double x, int n) { return (long double)ldexp((double)x,n); }






