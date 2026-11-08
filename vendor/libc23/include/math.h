/* C23 floating-point mathematics. */
#ifndef _LIBC23_MATH_H
#define _LIBC23_MATH_H
#define __STDC_VERSION_MATH_H__ 202311L

#include <float.h>
#include <limits.h>

#if FLT_EVAL_METHOD == 0
typedef float float_t;
typedef double double_t;
#elif FLT_EVAL_METHOD == 1
typedef double float_t;
typedef double double_t;
#elif FLT_EVAL_METHOD == 2
typedef long double float_t;
typedef long double double_t;
#else
typedef float float_t;
typedef double double_t;
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERREXCEPT
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 INT_MIN
#define FP_ILOGBNAN INT_MIN
#define FP_LLOGB0 LONG_MIN
#define FP_LLOGBNAN LONG_MIN
#define FP_INT_UPWARD 0
#define FP_INT_DOWNWARD 1
#define FP_INT_TOWARDZERO 2
#define FP_INT_TONEARESTFROMZERO 3
#define FP_INT_TONEAREST 4
int __libc_fpclassifyf(float);
int __libc_fpclassify(double);
int __libc_fpclassifyl(long double);
#define fpclassify(x) _Generic((x), float: __libc_fpclassifyf, long double: __libc_fpclassifyl, default: __libc_fpclassify)(x)
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) (fpclassify(x) == FP_NAN)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x,y) __builtin_isgreater((x),(y))
#define isgreaterequal(x,y) __builtin_isgreaterequal((x),(y))
#define isless(x,y) __builtin_isless((x),(y))
#define islessequal(x,y) __builtin_islessequal((x),(y))
#define islessgreater(x,y) __builtin_islessgreater((x),(y))
#define isunordered(x,y) __builtin_isunordered((x),(y))
#define iszero(x) (fpclassify(x) == FP_ZERO)
#define issubnormal(x) __builtin_isfpclass((x), 0x90)
#define iscanonical(x) _Generic((x), float: __libc_iscanonicalf, long double: __libc_iscanonicall, default: __libc_iscanonical)(x)
#define issignaling(x) _Generic((x), float: __libc_issignalingf, long double: __libc_issignalingl, default: __libc_issignaling)(x)
#define iseqsig(x,y) _Generic(((x)+(y)), float: __libc_iseqsigf, long double: __libc_iseqsigl, default: __libc_iseqsig)((x),(y))
#define __LIBC23_MATH_UNARY(n) double n(double); float n##f(float); long double n##l(long double)
#define __LIBC23_MATH_BINARY(n) double n(double,double); float n##f(float,float); long double n##l(long double,long double)
__LIBC23_MATH_UNARY(fabs);
__LIBC23_MATH_UNARY(sqrt);
__LIBC23_MATH_UNARY(trunc);
__LIBC23_MATH_UNARY(floor);
__LIBC23_MATH_UNARY(ceil);
__LIBC23_MATH_UNARY(round);
__LIBC23_MATH_UNARY(roundeven);
__LIBC23_MATH_BINARY(copysign);
__LIBC23_MATH_BINARY(fmod);
__LIBC23_MATH_BINARY(fdim);
__LIBC23_MATH_BINARY(fmax);
__LIBC23_MATH_BINARY(fmin);
__LIBC23_MATH_BINARY(fmaximum);
__LIBC23_MATH_BINARY(fminimum);
__LIBC23_MATH_BINARY(fmaximum_num);
__LIBC23_MATH_BINARY(fminimum_num);
__LIBC23_MATH_BINARY(fmaximum_mag);
__LIBC23_MATH_BINARY(fminimum_mag);
__LIBC23_MATH_BINARY(fmaximum_mag_num);
__LIBC23_MATH_BINARY(fminimum_mag_num);
__LIBC23_MATH_UNARY(exp);
__LIBC23_MATH_UNARY(exp2);
__LIBC23_MATH_UNARY(exp10);
__LIBC23_MATH_UNARY(exp2m1);
__LIBC23_MATH_UNARY(exp10m1);
__LIBC23_MATH_UNARY(expm1);
__LIBC23_MATH_UNARY(log);
__LIBC23_MATH_UNARY(log2);
__LIBC23_MATH_UNARY(log10);
__LIBC23_MATH_UNARY(logp1);
__LIBC23_MATH_UNARY(log2p1);
__LIBC23_MATH_UNARY(log10p1);
__LIBC23_MATH_UNARY(log1p);
__LIBC23_MATH_UNARY(sin);
__LIBC23_MATH_UNARY(cos);
__LIBC23_MATH_UNARY(tan);
__LIBC23_MATH_BINARY(pow);
__LIBC23_MATH_UNARY(sinh);
__LIBC23_MATH_UNARY(cosh);
__LIBC23_MATH_UNARY(tanh);
__LIBC23_MATH_UNARY(asinh);
__LIBC23_MATH_UNARY(acosh);
__LIBC23_MATH_UNARY(atanh);
__LIBC23_MATH_UNARY(atan);
__LIBC23_MATH_BINARY(atan2);
__LIBC23_MATH_UNARY(acospi);
__LIBC23_MATH_UNARY(asinpi);
__LIBC23_MATH_UNARY(atanpi);
__LIBC23_MATH_BINARY(atan2pi);
__LIBC23_MATH_UNARY(cospi);
__LIBC23_MATH_UNARY(sinpi);
__LIBC23_MATH_UNARY(tanpi);
__LIBC23_MATH_UNARY(nextup);
__LIBC23_MATH_UNARY(nextdown);
__LIBC23_MATH_UNARY(asin);
__LIBC23_MATH_UNARY(acos);
__LIBC23_MATH_BINARY(powr);
__LIBC23_MATH_UNARY(rsqrt);
double pown(double, long long); float pownf(float, long long); long double pownl(long double, long long);
double compoundn(double, long long); float compoundnf(float, long long); long double compoundnl(long double, long long);
double rootn(double, long long); float rootnf(float, long long); long double rootnl(long double, long long);
__LIBC23_MATH_BINARY(hypot);
__LIBC23_MATH_UNARY(erf);
__LIBC23_MATH_UNARY(erfc);
__LIBC23_MATH_UNARY(tgamma);
__LIBC23_MATH_UNARY(lgamma);
double cbrt(double); float cbrtf(float); long double cbrtl(long double);
double fma(double, double, double); float fmaf(float, float, float); long double fmal(long double, long double, long double);
float fadd(double, double); float faddl(long double, long double);
float fsub(double, double); float fsubl(long double, long double);
float fmul(double, double); float fmull(long double, long double);
float fdiv(double, double); float fdivl(long double, long double);
double daddl(long double, long double); double dsubl(long double, long double);
double dmull(long double, long double); double ddivl(long double, long double);
float ffma(double, double, double); float ffmal(long double, long double, long double);
double dfmal(long double, long double, long double);
float fsqrt(double); float fsqrtl(long double); double dsqrtl(long double);
double nan(const char *); float nanf(const char *); long double nanl(const char *);
double modf(double, double *); float modff(float, float *); long double modfl(long double, long double *);
double frexp(double, int *); float frexpf(float, int *); long double frexpl(long double, int *);
double ldexp(double, int); float ldexpf(float, int); long double ldexpl(long double, int);
double nextafter(double, double); float nextafterf(float, float); long double nextafterl(long double, long double);
double nexttoward(double, long double); float nexttowardf(float, long double); long double nexttowardl(long double, long double);
double remainder(double, double); float remainderf(float, float); long double remainderl(long double, long double);
double remquo(double, double, int *); float remquof(float, float, int *); long double remquol(long double, long double, int *);
double scalbn(double, int); float scalbnf(float, int); long double scalbnl(long double, int);
double scalbln(double, long); float scalblnf(float, long); long double scalblnl(long double, long);
double logb(double); float logbf(float); long double logbl(long double);
long llogb(double); long llogbf(float); long llogbl(long double);
int ilogb(double); int ilogbf(float); int ilogbl(long double);
double rint(double); float rintf(float); long double rintl(long double);
double nearbyint(double); float nearbyintf(float); long double nearbyintl(long double);
long lrint(double); long lrintf(float); long lrintl(long double);
long long llrint(double); long long llrintf(float); long long llrintl(long double);
long lround(double); long lroundf(float); long lroundl(long double);
long long llround(double); long long llroundf(float); long long llroundl(long double);
double fromfp(double, int, unsigned int); float fromfpf(float, int, unsigned int); long double fromfpl(long double, int, unsigned int);
double ufromfp(double, int, unsigned int); float ufromfpf(float, int, unsigned int); long double ufromfpl(long double, int, unsigned int);
double fromfpx(double, int, unsigned int); float fromfpxf(float, int, unsigned int); long double fromfpxl(long double, int, unsigned int);
double ufromfpx(double, int, unsigned int); float ufromfpxf(float, int, unsigned int); long double ufromfpxl(long double, int, unsigned int);
int canonicalize(double *, const double *); int canonicalizef(float *, const float *); int canonicalizel(long double *, const long double *);
int __libc_iscanonical(double); int __libc_iscanonicalf(float); int __libc_iscanonicall(long double);
int __libc_issignaling(double); int __libc_issignalingf(float); int __libc_issignalingl(long double);
int __libc_iseqsig(double, double); int __libc_iseqsigf(float, float); int __libc_iseqsigl(long double, long double);
#if defined(__STDC_WANT_IEC_60559_EXT__) && defined(__STDC_IEC_60559_BFP__)
int totalorder(const double *, const double *); int totalorderf(const float *, const float *); int totalorderl(const long double *, const long double *);
int totalordermag(const double *, const double *); int totalordermagf(const float *, const float *); int totalordermagl(const long double *, const long double *);
double getpayload(const double *); float getpayloadf(const float *); long double getpayloadl(const long double *);
int setpayload(double *, double); int setpayloadf(float *, float); int setpayloadl(long double *, long double);
int setpayloadsig(double *, double); int setpayloadsigf(float *, float); int setpayloadsigl(long double *, long double);
#endif
#undef __LIBC23_MATH_UNARY
#undef __LIBC23_MATH_BINARY
#ifdef __cplusplus
}
#endif
#endif


