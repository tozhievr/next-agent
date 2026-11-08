/* C23 base-10 and compensated exponential/logarithm entry points. */
#include "libm.h"

/* Split constants retain the low part lost in multiplication. */
static const double ln2_hi = 0x1.62e42fee00000p-1;
static const double ln2_lo = 0x1.a39ef35793c76p-33;
static const double ln10_hi = 0x1.26bb1bbb00000p+1;
static const double ln10_lo = 0x1.554560b752b6bp-33;
static const double log10_2_hi = 0x1.34413509f6000p-2;
static const double log10_2_lo = 0x1.9fef311f12b36p-42;

/* Reduce in base 10 before multiplying by ln(10), so an intermediate
 * exponential cannot overflow when the final answer is representable. */
double exp10(double x) {
    if (isnan(x)) return x+x;
    if (x == INFINITY) return x;
    if (x == -INFINITY) return 0.0;
    if (x == 0.0) return 1.0;
    /* Powers 10^n through n=22 are exactly representable in binary64. */
    if (x >= 0.0 && x <= 22.0 && x == (double)(int)x) {
        double p = 1.0;
        for (int i = 0; i < (int)x; ++i) p *= 10.0;
        return p;
    }
    if (x > 309.0) return __math_oflow(0);
    if (x < -325.0) return __math_uflow(0);
    int k = (int)(x * 3.32192809488736234787 + (x < 0 ? -0.5 : 0.5));
    double r = (x - k*log10_2_hi) - k*log10_2_lo;
    double a = r*ln10_hi, b = r*ln10_lo;
    return scalbn(exp(a) * (1.0 + expm1(b)), k);
}

double exp2m1(double x) {
    if (x == 0.0 || isnan(x)) return x+x;
    if (x == -INFINITY) return -1.0;
    if (x == INFINITY) return x;
    if (x < -55.0) { volatile double tiny = 0x1p-100; return tiny-1.0; }
    if (fabs(x) > 0.5) return exp2(x)-1.0;
    double a = x*ln2_hi, b = x*ln2_lo;
    double y = expm1(a);
    return y + (1.0+y)*expm1(b);
}
double exp10m1(double x) {
    if (x == 0.0 || isnan(x)) return x+x;
    if (x == -INFINITY) return -1.0;
    if (x == INFINITY) return x;
    /* 10^n-1 also remains an exactly representable integer through n=15. */
    if (x > 0.0 && x <= 15.0 && x == (double)(int)x) {
        uint64_t p = 1;
        for (int i = 0; i < (int)x; ++i) p *= 10;
        return (double)(p - 1);
    }
    if (x < -17.0) { volatile double tiny = 0x1p-100; return tiny-1.0; }
    if (fabs(x) > 0.5) return exp10(x)-1.0;
    double a = x*ln10_hi, b = x*ln10_lo;
    double y = expm1(a);
    return y + (1.0+y)*expm1(b);
}
double logp1(double x) { return log1p(x); }
double log2p1(double x) { return log1p(x)*1.44269504088896340736; }
double log10p1(double x) { return log1p(x)*0.43429448190325182765; }

#define WRAP(n) float n##f(float x) { return (float)n((double)x); } \
    long double n##l(long double x) { return (long double)n((double)x); }
WRAP(exp10) WRAP(exp10m1) WRAP(exp2m1)
WRAP(logp1) WRAP(log2p1) WRAP(log10p1)
