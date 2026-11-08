/* C23 arithmetic with direct rounding to the narrower result type. */
#include "libm.h"
#include <fenv.h>

float __libc_narrow_fma(double, double, double);
float fadd(double x,double y) { return __libc_narrow_fma(x,1.0,y); }
float fsub(double x,double y) { return __libc_narrow_fma(x,1.0,-y); }
float fmul(double x,double y) {
    double zero=asdouble((asuint64(x)^asuint64(y)) & 0x8000000000000000ULL);
    return __libc_narrow_fma(x,y,zero);
}
float ffma(double x,double y,double z) { return __libc_narrow_fma(x,y,z); }

/* First compute toward zero, retaining a sticky bit in the least significant
 * binary64 bit. The final conversion then rounds once to binary32. */
static float narrow_divsqrt(double x,double y,int root) {
    fenv_t env;
    feholdexcept(&env);
    fesetround(FE_TOWARDZERO);
    volatile double a=x,b=y;
    volatile double r=root ? sqrt(a) : a/b;
    int raised=fetestexcept(FE_ALL_EXCEPT);
    uint64_t bits=asuint64(r);
    if (raised & FE_OVERFLOW) bits=(bits & 0x8000000000000000ULL) | 0x7fefffffffffffffULL;
    else if ((raised & FE_INEXACT) && isfinite(r)) bits |= 1;
    fesetenv(&env);
    if (raised & (FE_INVALID|FE_DIVBYZERO)) feraiseexcept(raised & (FE_INVALID|FE_DIVBYZERO));
    volatile double rounded=asdouble(bits);
    volatile float out=(float)rounded;
    return out;
}
float fdiv(double x,double y) { return narrow_divsqrt(x,y,0); }
float fsqrt(double x) { return narrow_divsqrt(x,0,1); }
float faddl(long double x,long double y) { return fadd((double)x,(double)y); }
float fsubl(long double x,long double y) { return fsub((double)x,(double)y); }
float fmull(long double x,long double y) { return fmul((double)x,(double)y); }
float fdivl(long double x,long double y) { return fdiv((double)x,(double)y); }
float ffmal(long double x,long double y,long double z) { return ffma((double)x,(double)y,(double)z); }
float fsqrtl(long double x) { return fsqrt((double)x); }
double daddl(long double x,long double y) { return (double)x+(double)y; }
double dsubl(long double x,long double y) { return (double)x-(double)y; }
double dmull(long double x,long double y) { return (double)x*(double)y; }
double ddivl(long double x,long double y) { return (double)x/(double)y; }
double dfmal(long double x,long double y,long double z) { return fma((double)x,(double)y,(double)z); }
double dsqrtl(long double x) { return sqrt((double)x); }
