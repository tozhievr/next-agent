/* Inverse trigonometric functions using argument reduction and a convergent series. */
#include "libm.h"

static const double pi = 0x1.921fb54442d18p+1;
static const double pio2 = 0x1.921fb54442d18p+0;
static const double pio4 = 0x1.921fb54442d18p-1;

double atan(double x) {
    if (isnan(x)) return x + x;
    if (x == 0.0) return x;
    double a = fabs(x), base = 0.0, z;
    int invert = a > 1.0;
    if (invert) a = 1.0 / a;
    if (a > 0x1.a827999fcef32p-2) { z = (a - 1.0) / (a + 1.0); base = pio4; }
    else z = a;
    double zz = z * z, term = z, sum = z;
    for (int i = 1; i <= 28; ++i) { term *= -zz; sum += term / (2*i + 1); }
    double result = base + sum;
    if (invert) result = pio2 - result;
    return copysign(result, x);
}
double atan2(double y, double x) {
    if (isnan(x) || isnan(y)) return x + y;
    int sy = signbit(y), sx = signbit(x);
    if (y == 0.0) return sx ? copysign(pi, y) : y;
    if (x == 0.0) return copysign(pio2, y);
    if (isinf(y)) {
        double a = isinf(x) ? (sx ? 3.0 * pio4 : pio4) : pio2;
        return copysign(a, y);
    }
    if (isinf(x)) return copysign(sx ? pi : 0.0, y);
    double ax = fabs(x), ay = fabs(y);
    double a = ay > ax ? pio2 - atan(ax / ay) : atan(ay / ax);
    if (sx) a = pi - a;
    return sy ? -a : a;
}
double asin(double x) {
    if (isnan(x)) return x + x;
    if (fabs(x) > 1.0) return __math_invalid(x);
    if (x == 0.0) return x;
    return atan2(x, sqrt((1.0 - x) * (1.0 + x)));
}
double acos(double x) {
    if (isnan(x)) return x + x;
    if (fabs(x) > 1.0) return __math_invalid(x);
    return atan2(sqrt((1.0 - x) * (1.0 + x)), x);
}
#define UNARY(n) float n##f(float x) { return (float)n(x); } long double n##l(long double x) { return (long double)n((double)x); }
UNARY(atan) UNARY(asin) UNARY(acos)
float atan2f(float y, float x) { return (float)atan2(y,x); }
long double atan2l(long double y, long double x) { return (long double)atan2((double)y,(double)x); }
