/* C23 type-generic mathematics. */
#ifndef _LIBC23_TGMATH_H
#define _LIBC23_TGMATH_H
#define __STDC_VERSION_TGMATH_H__ 202311L

#include <math.h>
#include <complex.h>

#if !defined(__cplusplus)

/* The controlling expressions of _Generic are not evaluated.  Each argument
 * is therefore evaluated exactly once, in the selected function call. */
#define __TGM_REAL1(x, n) \
    _Generic((x), float: n##f, long double: n##l, default: n)((x))
#define __TGM_T(x) \
    __extension__ _Generic((x), float: (float)0, double: (double)0, long double: (long double)0, \
        float complex: (float complex)0, double complex: (double complex)0, \
        long double complex: (long double complex)0, default: (double)0)
#define __TGM_REAL2(x, y, n) \
    _Generic(__TGM_T(x) + __TGM_T(y), float: n##f, long double: n##l, default: n)((x), (y))
#define __TGM_REAL3(x, y, z, n) \
    _Generic(__TGM_T(x) + __TGM_T(y) + __TGM_T(z), float: n##f, long double: n##l, default: n)((x), (y), (z))
#define __TGM_REAL2PTR(x, y, p, n) \
    _Generic(__TGM_T(x) + __TGM_T(y), float: n##f, long double: n##l, default: n)((x), (y), (p))
#define __TGM_CPLX1(x, n, c) \
    __extension__ _Generic((x), \
        float: c##f, double: c, long double: c##l, \
        float complex: c##f, double complex: c, long double complex: c##l, \
        default: c)((x))
#define __TGM_MIX1(x, realfn, cplxfn) \
    __extension__ _Generic((x), \
        float: realfn##f, double: realfn, long double: realfn##l, \
        float complex: cplxfn##f, double complex: cplxfn, long double complex: cplxfn##l, \
        default: realfn)((x))
#define __TGM_MIX2(x, y, realfn, cplxfn) \
    __extension__ _Generic(__TGM_T(x) + __TGM_T(y), \
        float: realfn##f, long double: realfn##l, \
        float complex: cplxfn##f, double complex: cplxfn, long double complex: cplxfn##l, \
        default: realfn)((x), (y))
#define __TGM_FIRST2(x, y, n) \
    _Generic((x), float: n##f, long double: n##l, default: n)((x), (y))
#define __TGM_FIRST3(x, y, z, n) \
    _Generic((x), float: n##f, long double: n##l, default: n)((x), (y), (z))
#define __TGM_NARROW1(x, n) \
    _Generic((x), long double: n##l, default: n)((x))
#define __TGM_NARROW2(x, y, n) \
    _Generic(__TGM_T(x) + __TGM_T(y), long double: n##l, default: n)((x), (y))
#define __TGM_NARROW3(x, y, z, n) \
    _Generic(__TGM_T(x) + __TGM_T(y) + __TGM_T(z), long double: n##l, default: n)((x), (y), (z))
#define __TGM_PTR1(p, n) \
    _Generic((p), \
        float *: n##f, const float *: n##f, volatile float *: n##f, const volatile float *: n##f, \
        double *: n, const double *: n, volatile double *: n, const volatile double *: n, \
        long double *: n##l, const long double *: n##l, volatile long double *: n##l, const volatile long double *: n##l, \
        default: n)((p))
#define __TGM_PTR2(p, q, n) \
    _Generic((p), \
        float *: n##f, const float *: n##f, volatile float *: n##f, const volatile float *: n##f, \
        double *: n, const double *: n, volatile double *: n, const volatile double *: n, \
        long double *: n##l, const long double *: n##l, volatile long double *: n##l, const volatile long double *: n##l, \
        default: n)((p), (q))

/* Real functions with no complex counterpart. */
#define fabs(x)       __TGM_MIX1((x), fabs, cabs)
#define trunc(x)      __TGM_REAL1((x), trunc)
#define floor(x)      __TGM_REAL1((x), floor)
#define ceil(x)       __TGM_REAL1((x), ceil)
#define round(x)      __TGM_REAL1((x), round)
#define roundeven(x)  __TGM_REAL1((x), roundeven)
#define cbrt(x)       __TGM_REAL1((x), cbrt)
#define rsqrt(x)      __TGM_REAL1((x), rsqrt)
#define exp2(x)       __TGM_REAL1((x), exp2)
#define exp10(x)      __TGM_REAL1((x), exp10)
#define expm1(x)      __TGM_REAL1((x), expm1)
#define exp2m1(x)     __TGM_REAL1((x), exp2m1)
#define exp10m1(x)    __TGM_REAL1((x), exp10m1)
#define log2(x)       __TGM_REAL1((x), log2)
#define log10(x)      __TGM_REAL1((x), log10)
#define log1p(x)      __TGM_REAL1((x), log1p)
#define logp1(x)      __TGM_REAL1((x), logp1)
#define log2p1(x)     __TGM_REAL1((x), log2p1)
#define log10p1(x)    __TGM_REAL1((x), log10p1)
#define sinpi(x)      __TGM_REAL1((x), sinpi)
#define cospi(x)      __TGM_REAL1((x), cospi)
#define tanpi(x)      __TGM_REAL1((x), tanpi)
#define asinpi(x)     __TGM_REAL1((x), asinpi)
#define acospi(x)     __TGM_REAL1((x), acospi)
#define atanpi(x)     __TGM_REAL1((x), atanpi)
#define nextup(x)     __TGM_REAL1((x), nextup)
#define nextdown(x)   __TGM_REAL1((x), nextdown)
#define llogb(x)      __TGM_REAL1((x), llogb)
#define copysign(x,y) __TGM_REAL2((x),(y),copysign)
#define fmod(x,y)     __TGM_REAL2((x),(y),fmod)
#define fmax(x,y)     __TGM_REAL2((x),(y),fmax)
#define fmin(x,y)     __TGM_REAL2((x),(y),fmin)
#define fdim(x,y)     __TGM_REAL2((x),(y),fdim)
#define fmaximum(x,y) __TGM_REAL2((x),(y),fmaximum)
#define fminimum(x,y) __TGM_REAL2((x),(y),fminimum)
#define fmaximum_num(x,y) __TGM_REAL2((x),(y),fmaximum_num)
#define fminimum_num(x,y) __TGM_REAL2((x),(y),fminimum_num)
#define fmaximum_mag(x,y) __TGM_REAL2((x),(y),fmaximum_mag)
#define fminimum_mag(x,y) __TGM_REAL2((x),(y),fminimum_mag)
#define fmaximum_mag_num(x,y) __TGM_REAL2((x),(y),fmaximum_mag_num)
#define fminimum_mag_num(x,y) __TGM_REAL2((x),(y),fminimum_mag_num)
#define atan2(x,y)    __TGM_REAL2((x),(y),atan2)
#define atan2pi(x,y)  __TGM_REAL2((x),(y),atan2pi)
#define hypot(x,y)    __TGM_REAL2((x),(y),hypot)
#define powr(x,y)     __TGM_REAL2((x),(y),powr)
#define pown(x,n)     __TGM_FIRST2((x),(n),pown)
#define compoundn(x,n) __TGM_FIRST2((x),(n),compoundn)
#define rootn(x,n)    __TGM_FIRST2((x),(n),rootn)
#define fma(x,y,z)    __TGM_REAL3((x),(y),(z),fma)
#define frexp(x,y)    __TGM_FIRST2((x),(y),frexp)
#define ldexp(x,y)    __TGM_FIRST2((x),(y),ldexp)
#define nextafter(x,y) __TGM_REAL2((x),(y),nextafter)
#define nexttoward(x,y) __TGM_FIRST2((x),(y),nexttoward)
#define remainder(x,y) __TGM_REAL2((x),(y),remainder)
#define remquo(x,y,z) __TGM_REAL2PTR((x),(y),(z),remquo)
#define scalbn(x,y)   __TGM_FIRST2((x),(y),scalbn)
#define scalbln(x,y)  __TGM_FIRST2((x),(y),scalbln)
#define logb(x)       __TGM_REAL1((x),logb)
#define ilogb(x)      __TGM_REAL1((x),ilogb)
#define rint(x)       __TGM_REAL1((x),rint)
#define nearbyint(x)  __TGM_REAL1((x),nearbyint)
#define lrint(x)      __TGM_REAL1((x),lrint)
#define llrint(x)     __TGM_REAL1((x),llrint)
#define lround(x)     __TGM_REAL1((x),lround)
#define llround(x)    __TGM_REAL1((x),llround)
#define erf(x)        __TGM_REAL1((x),erf)
#define erfc(x)       __TGM_REAL1((x),erfc)
#define tgamma(x)     __TGM_REAL1((x),tgamma)
#define lgamma(x)     __TGM_REAL1((x),lgamma)
#define fromfp(x,r,w) __TGM_FIRST3((x),(r),(w),fromfp)
#define fromfpx(x,r,w) __TGM_FIRST3((x),(r),(w),fromfpx)
#define ufromfp(x,r,w) __TGM_FIRST3((x),(r),(w),ufromfp)
#define ufromfpx(x,r,w) __TGM_FIRST3((x),(r),(w),ufromfpx)

#if defined(__STDC_WANT_IEC_60559_EXT__) && defined(__STDC_IEC_60559_BFP__)
#define totalorder(x,y) __TGM_PTR2((x),(y),totalorder)
#define totalordermag(x,y) __TGM_PTR2((x),(y),totalordermag)
#define getpayload(x) __TGM_PTR1((x),getpayload)
#define setpayload(p,x) _Generic(__TGM_T(x), float: setpayloadf, long double: setpayloadl, default: setpayload)((p),(x))
#define setpayloadsig(p,x) _Generic(__TGM_T(x), float: setpayloadsigf, long double: setpayloadsigl, default: setpayloadsig)((p),(x))
#endif

/* C23 operations whose result is rounded to a narrower floating type. */
#define fadd(x,y)  __TGM_NARROW2((x),(y),fadd)
#define fsub(x,y)  __TGM_NARROW2((x),(y),fsub)
#define fmul(x,y)  __TGM_NARROW2((x),(y),fmul)
#define fdiv(x,y)  __TGM_NARROW2((x),(y),fdiv)
#define dadd(x,y)  daddl((x),(y))
#define dsub(x,y)  dsubl((x),(y))
#define dmul(x,y)  dmull((x),(y))
#define ddiv(x,y)  ddivl((x),(y))
#define ffma(x,y,z) __TGM_NARROW3((x),(y),(z),ffma)
#define dfma(x,y,z) dfmal((x),(y),(z))
#define fsqrt(x)   __TGM_NARROW1((x),fsqrt)
#define dsqrt(x)   dsqrtl((x))

/* Real functions that also have complex overloads in <complex.h>. */
#define sqrt(x)       __TGM_MIX1((x),sqrt,csqrt)
#define exp(x)        __TGM_MIX1((x),exp,cexp)
#define log(x)        __TGM_MIX1((x),log,clog)
#define sin(x)        __TGM_MIX1((x),sin,csin)
#define cos(x)        __TGM_MIX1((x),cos,ccos)
#define tan(x)        __TGM_MIX1((x),tan,ctan)
#define sinh(x)       __TGM_MIX1((x),sinh,csinh)
#define cosh(x)       __TGM_MIX1((x),cosh,ccosh)
#define tanh(x)       __TGM_MIX1((x),tanh,ctanh)
#define asin(x)       __TGM_MIX1((x),asin,casin)
#define acos(x)       __TGM_MIX1((x),acos,cacos)
#define atan(x)       __TGM_MIX1((x),atan,catan)
#define asinh(x)      __TGM_MIX1((x),asinh,casinh)
#define acosh(x)      __TGM_MIX1((x),acosh,cacosh)
#define atanh(x)      __TGM_MIX1((x),atanh,catanh)
#define pow(x,y)      __TGM_MIX2((x),(y),pow,cpow)

/* Complex-specific names remain type generic over their real precision. */
#define creal(x)      __TGM_CPLX1((x),creal,creal)
#define cimag(x)      __TGM_CPLX1((x),cimag,cimag)
#define conj(x)       __TGM_CPLX1((x),conj,conj)
#define cproj(x)      __TGM_CPLX1((x),cproj,cproj)
#define carg(x)       __TGM_CPLX1((x),carg,carg)

#endif

#endif
