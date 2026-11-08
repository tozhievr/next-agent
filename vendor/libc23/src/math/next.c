#include "libm.h"
#include <fenv.h>
#include <limits.h>

double nextafter(double x, double y) {
    uint64_t u = asuint64(x);
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    if (x == 0.0) u = (asuint64(y) & 0x8000000000000000ULL) | 1;
    else if ((x < y) == (x > 0)) ++u; else --u;
    double r = asdouble(u);
    if (isinf(r)) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    else if (fpclassify(r) == FP_SUBNORMAL || r == 0.0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    return r;
}
float nextafterf(float x, float y) {
    uint32_t u = asuint(x);
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    if (x == 0.0f) u = (asuint(y) & 0x80000000U) | 1;
    else if ((x < y) == (x > 0)) ++u; else --u;
    float r = asfloat(u);
    if (isinf(r)) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    else if (fpclassify(r) == FP_SUBNORMAL || r == 0.0f) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    return r;
}
long double nextafterl(long double x, long double y) {
    if (isnan(x) || isnan(y)) return x+y;
    if (x==y) return y;
    long double r=x<y ? nextupl(x) : nextdownl(x);
    if (isinf(r)) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
    else if (fpclassify(r)==FP_SUBNORMAL || r==0.0L) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    return r;
}
double nexttoward(double x, long double y) { if (isnan(x) || isnan(y)) return x + (double)y; if ((long double)x == y) return (double)y; return nextafter(x, x < y ? INFINITY : -INFINITY); }
float nexttowardf(float x, long double y) { if (isnan(x) || isnan(y)) return x + (float)y; if ((long double)x == y) return (float)y; return nextafterf(x, x < y ? INFINITY : -INFINITY); }
long double nexttowardl(long double x, long double y) { return nextafterl(x,y); }

double scalbn(double x, int n) { return ldexp(x,n); }
float scalbnf(float x, int n) { return ldexpf(x,n); }
long double scalbnl(long double x, int n) { return ldexpl(x,n); }
double scalbln(double x, long n) { return ldexp(x, n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n); }
float scalblnf(float x, long n) { return ldexpf(x, n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n); }
long double scalblnl(long double x, long n) { return ldexpl(x, n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n); }
int ilogb(double x) {
    int e;
    if (x == 0.0) { feraiseexcept(FE_INVALID); return FP_ILOGB0; }
    if (isnan(x)) { feraiseexcept(FE_INVALID); return FP_ILOGBNAN; }
    if (isinf(x)) { feraiseexcept(FE_INVALID); return INT_MAX; }
    frexp(x, &e); return e - 1;
}
int ilogbf(float x) { return ilogb(x); }
int ilogbl(long double x) { return ilogb((double)x); }
double logb(double x) { if (x == 0) return __math_divzero(1); if (isnan(x)) return x+x; if (isinf(x)) return INFINITY; return (double)ilogb(x); }
float logbf(float x) { return (float)logb(x); }
long double logbl(long double x) { return (long double)logb((double)x); }


