#include "libm.h"
#include <fenv.h>
#include <limits.h>

double rint(double x) {
    if (!isfinite(x)) return isnan(x) ? x + x : x;
    double t = trunc(x), d = x - t;
    if (d == 0.0) return x;
    int mode = fegetround();
    double r = t;
    if (mode == FE_UPWARD && d > 0) r = t + 1;
    else if (mode == FE_DOWNWARD && d < 0) r = t - 1;
    else if (mode == FE_TONEAREST) {
        double a = fabs(d);
        if (a > 0.5 || (a == 0.5 && fmod(fabs(t), 2.0) == 1.0)) r = t + copysign(1.0, x);
    }
    feraiseexcept(FE_INEXACT);
    return r == 0.0 ? copysign(0.0,x) : r;
}
double nearbyint(double x) {
    fenv_t env;
    feholdexcept(&env);
    double r = rint(x);
    feclearexcept(FE_INEXACT);
    feupdateenv(&env);
    return r;
}
#define UNARY(n) float n##f(float x) { return (float)n(x); } long double n##l(long double x) { return (long double)n((double)x); }
UNARY(rint) UNARY(nearbyint)
/* Check in floating point before converting: out-of-range casts are undefined. */
static long to_long(double x) {
    const double limit = sizeof(long) == 4 ? 0x1p31 : 0x1p63;
    if (!isfinite(x) || x < -limit || x >= limit) {
        feraiseexcept(FE_INVALID);
        return LONG_MIN;
    }
    return (long)x;
}
static long long to_llong(double x) {
    const double limit = 0x1p63;
    if (!isfinite(x) || x < -limit || x >= limit) {
        feraiseexcept(FE_INVALID);
        return LLONG_MIN;
    }
    return (long long)x;
}
long lrint(double x) { return to_long(rint(x)); }
long lrintf(float x) { return lrint(x); }
long lrintl(long double x) { return lrint((double)x); }
long long llrint(double x) { return to_llong(rint(x)); }
long long llrintf(float x) { return llrint(x); }
long long llrintl(long double x) { return llrint((double)x); }
long lround(double x) { return to_long(round(x)); }
long lroundf(float x) { return lround(x); }
long lroundl(long double x) { return lround((double)x); }
long long llround(double x) { return to_llong(round(x)); }
long long llroundf(float x) { return llround(x); }
long long llroundl(long double x) { return llround((double)x); }


