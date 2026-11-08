#include "libm.h"

double remquo(double x, double y, int *quo) {
    int ex, ey, q = 0, negative = signbit(x) != signbit(y);
    double ax = fabs(x), ay = fabs(y), r = ax;
    *quo = 0;
    if (isnan(x) || isnan(y)) return x + y;
    if (isinf(x) || y == 0.0) return __math_invalid(x * y);
    if (isinf(y) || x == 0.0) return x;
    double mx = frexp(ax, &ex), my = frexp(ay, &ey);
    int shift = ex - ey;
    if (mx < my) shift--;
    if (shift >= 0) {
        double scaled = ldexp(ay, shift);

        for (; shift >= 0; --shift) {
            q = (q << 1) & 127;
            if (r >= scaled) { r -= scaled; q |= 1; }
            if (shift) scaled *= 0.5;
        }
    }
    if (r > ay - r || (r == ay - r && (q & 1))) { r -= ay; q = (q + 1) & 127; }
    *quo = negative ? -q : q;
    if (signbit(x)) r = -r;
    return r == 0.0 ? copysign(0.0, x) : r;
}
double remainder(double x, double y) { int q; return remquo(x,y,&q); }
float remquof(float x, float y, int *q) { return (float)remquo(x,y,q); }
long double remquol(long double x, long double y, int *q) { return (long double)remquo((double)x,(double)y,q); }
float remainderf(float x, float y) { return (float)remainder(x,y); }
long double remainderl(long double x, long double y) { return (long double)remainder((double)x,(double)y); }


