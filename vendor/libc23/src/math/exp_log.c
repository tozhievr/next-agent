/* Argument reduction and polynomial kernels adapted from fdlibm/OpenLibm.
 * Copyright (C) 1993, 2004 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 */
#include "libm.h"
#include <fenv.h>

static const double ln2hi = 6.93147180369123816490e-01;
static const double ln2lo = 1.90821492927058770002e-10;
static const double invln2 = 1.44269504088896338700e+00;

/* fdlibm rational approximation after reduction to [-ln(2)/2, ln(2)/2]. */
double exp(double x) {
    uint64_t u = asuint64(x), ax = u & UINT64_C(0x7fffffffffffffff);
    int neg = (int)(u >> 63), k = 0;
    double hi = 0, lo = 0, t, c, y;
    if (ax >= UINT64_C(0x7ff0000000000000)) {
        if (ax > UINT64_C(0x7ff0000000000000)) return x + x;
        return neg ? 0.0 : x;
    }
    if (x > 7.09782712893383973096e+02) return __math_oflow(0);
    if (x < -7.45133219101941108420e+02) return __math_uflow(0);
    if (ax > UINT64_C(0x3fd62e4200000000)) {
        k = (int)(x * invln2 + (neg ? -0.5 : 0.5));
        hi = x - k * ln2hi; lo = k * ln2lo; x = hi - lo;
    } else if (ax < UINT64_C(0x3e30000000000000)) return 1.0 + x;
    t = x*x;
    c = x - t*(1.66666666666666019037e-01 + t*(-2.77777777770155933842e-03 +
        t*(6.61375632143793436117e-05 + t*(-1.65339022054652515390e-06 + t*4.13813679705723846039e-08))));
    if (!k) return 1.0 - ((x*c)/(c-2.0)-x);
    y = 1.0 - ((lo - x*c/(2.0-c)) - hi);
    return scalbn(y, k);
}

double expm1(double x) {
    uint64_t u = asuint64(x), ax = u & UINT64_C(0x7fffffffffffffff);
    int neg = (int)(u >> 63), k = 0;
    double hi, lo, c = 0, t, e, hxs, hfx, r1, twopk, y;
    if (!ax) return x;
    if (ax >= UINT64_C(0x7ff0000000000000)) {
        if (ax > UINT64_C(0x7ff0000000000000)) return x+x;
        return neg ? -1.0 : x;
    }
    if (x > 7.09782712893383973096e+02) return __math_oflow(0);
    if (x < -56.0 * ln2hi) { volatile double tiny = 0x1p-100; return tiny - 1.0; }
    /* For |x| <= 2^-52, expm1(x) lies between x and its successor. */
    if (ax && ax <= UINT64_C(0x3cb0000000000000)) {
        int mode = fegetround();
        if (mode == FE_UPWARD) return nextafter(x, INFINITY);
        if (mode == FE_DOWNWARD) return x;
        if (mode == FE_TOWARDZERO) return neg ? nextafter(x, INFINITY) : x;
        /* Below 2^-54 the exact correction is far below half an ulp, so
         * nearest returns x.  Keep the inexact indication for nonzero x. */
        if (mode == FE_TONEAREST && ax < UINT64_C(0x3c90000000000000)) {
            fp_force_eval_d(1.0 + x);
            return x;
        }
        if (mode == FE_TONEAREST) {
            double up = nextafter(x, INFINITY);
            double half_gap = (up - x) * 0.5;
            double quadratic = (0.5 * x) * x;
            double product_error = fma(0.5 * x, x, -quadratic);
            double cubic_tail = quadratic * x * (1.0 / 3.0);
            if (quadratic > half_gap) return up;
            if (quadratic < half_gap) return x;
            double remainder = product_error + cubic_tail;
            if (remainder > 0.0) return up;
            if (remainder < 0.0) return x;
            return (asuint64(x) & 1) ? up : x;
        }
    }
    if (ax > UINT64_C(0x3fd62e4200000000)) {
        k = (int)(invln2*x + (neg ? -0.5 : 0.5));
        hi = x-k*ln2hi; lo = k*ln2lo; x = hi-lo; c = (hi-x)-lo;
    }
    hfx = 0.5*x; hxs = x*hfx;
    r1 = 1.0+hxs*(-3.33333333333331316428e-02 + hxs*(1.58730158725481460165e-03 +
         hxs*(-7.93650757867487942473e-05 + hxs*(4.00821782732936239552e-06 + hxs*-2.01099218183624371326e-07))));
    t = 3.0-r1*hfx; e = hxs*((r1-t)/(6.0-x*t));
    if (!k) return x-(x*e-hxs);
    e = x*(e-c)-c-hxs;
    if (k == -1) return 0.5*(x-e)-0.5;
    if (k == 1) return x < -0.25 ? -2.0*(e-(x+0.5)) : 1.0+2.0*(x-e);
    if (k < 0 || k > 56) return scalbn(1.0+x-e,k)-1.0;
    twopk = asdouble((uint64_t)(1023+k)<<52);
    y = asdouble((uint64_t)(1023-k)<<52);
    return (k < 20 ? x-e+(1.0-y) : x-(e+y)+1.0)*twopk;
}

/* Logarithm kernel from fdlibm: log(1+f) on the reduced interval. */
static double log_kernel(double f) {
    double s=f/(2.0+f), z=s*s, w=z*z;
    double r=w*(3.999999999940941908e-01+w*(2.222219843214978396e-01+w*1.531383769920937332e-01)) +
        z*(6.666666666666735130e-01+w*(2.857142874366239149e-01+w*(1.818357216161805012e-01+w*1.479819860511658591e-01)));
    double h=0.5*f*f;
    return f-(h-s*(h+r));
}
static double normalized_log(double x, int *exponent) {
    uint64_t u = asuint64(x);
    int k = 0;
    if ((u >> 52) == 0) { x *= 0x1p54; u=asuint64(x); k -= 54; }
    k += (int)(u >> 52)-1023;
    u = (u & UINT64_C(0x000fffffffffffff)) | UINT64_C(0x3ff0000000000000);
    x = asdouble(u);
    if (x >= 1.4142135623730950488) { x *= 0.5; ++k; }
    *exponent=k;
    return log_kernel(x-1.0);
}
double log(double x) {
    uint64_t u=asuint64(x), a=u & UINT64_C(0x7fffffffffffffff);
    if (a > UINT64_C(0x7ff0000000000000)) return x+x;
    if (!a) return __math_divzero(1);
    if (u >> 63) return __math_invalid(x);
    if (a == UINT64_C(0x7ff0000000000000)) return x;
    int k; double f=normalized_log(x,&k);
    return k*ln2hi + (f + k*ln2lo);
}
double log1p(double x) {
    if (isnan(x)) return x+x;
    if (x == -1.0) return __math_divzero(1);
    if (x < -1.0) return __math_invalid(x);
    if (isinf(x)) return x;
    if (x == 0.0) return x;
    if (fabs(x) < 0x1p-54) { fp_force_eval_d(1.0+x); return x; }
    if (fabs(x) < 0.2928932188134524) return log_kernel(x);
    double u=1.0+x;
    double correction = x > 1.0 ? 1.0-(u-x) : x-(u-1.0);
    return log(u) + correction/u;
}
double log2(double x) {
    if (!(x > 0.0) || isinf(x)) return log(x);
    int k; double f=normalized_log(x,&k);
    return (double)k + f*invln2;
}
double log10(double x) { return log(x) * 0.434294481903251827651128918916605082; }
double exp2(double x) {
    if (isnan(x)) return x+x;
    if (x == INFINITY) return x;
    if (x == -INFINITY) return 0.0;
    if (x >= 1024.0) return __math_oflow(0);
    if (x <= -1075.0) return __math_uflow(0);
    int k = (int)(x + (x < 0 ? -0.5 : 0.5));
    double r = x-k;
    return scalbn(exp(r*ln2hi+r*ln2lo),k);
}

#define WRAP(n) float n##f(float x) { return (float)n((double)x); } \
    long double n##l(long double x) { return (long double)n((double)x); }
WRAP(exp) WRAP(exp2) WRAP(expm1) WRAP(log) WRAP(log2) WRAP(log10) WRAP(log1p)

