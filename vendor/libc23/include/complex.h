/* C23 complex arithmetic. */
#ifndef _LIBC23_COMPLEX_H
#define _LIBC23_COMPLEX_H
#define __STDC_VERSION_COMPLEX_H__ 202311L
#define complex _Complex
#define _Complex_I (__extension__ 1.0fi)
#define I _Complex_I
#define CMPLX(x,y) __builtin_complex((double)(x), (double)(y))
#define CMPLXF(x,y) __builtin_complex((float)(x), (float)(y))
#define CMPLXL(x,y) __builtin_complex((long double)(x), (long double)(y))
#ifdef __cplusplus
extern "C" {
#endif
#define __LIBC23_COMPLEX_UNARY(n) double complex n(double complex); float complex n##f(float complex); long double complex n##l(long double complex)
#define __LIBC23_COMPLEX_REAL(n) double n(double complex); float n##f(float complex); long double n##l(long double complex)
__LIBC23_COMPLEX_REAL(creal); __LIBC23_COMPLEX_REAL(cimag); __LIBC23_COMPLEX_REAL(cabs); __LIBC23_COMPLEX_REAL(carg);
__LIBC23_COMPLEX_UNARY(conj); __LIBC23_COMPLEX_UNARY(cproj); __LIBC23_COMPLEX_UNARY(csqrt);
__LIBC23_COMPLEX_UNARY(cexp); __LIBC23_COMPLEX_UNARY(clog);
__LIBC23_COMPLEX_UNARY(csin); __LIBC23_COMPLEX_UNARY(ccos); __LIBC23_COMPLEX_UNARY(ctan);
__LIBC23_COMPLEX_UNARY(csinh); __LIBC23_COMPLEX_UNARY(ccosh); __LIBC23_COMPLEX_UNARY(ctanh);
__LIBC23_COMPLEX_UNARY(casin); __LIBC23_COMPLEX_UNARY(cacos); __LIBC23_COMPLEX_UNARY(catan);
__LIBC23_COMPLEX_UNARY(casinh); __LIBC23_COMPLEX_UNARY(cacosh); __LIBC23_COMPLEX_UNARY(catanh);
double complex cpow(double complex, double complex);
float complex cpowf(float complex, float complex);
long double complex cpowl(long double complex, long double complex);
#undef __LIBC23_COMPLEX_REAL
#undef __LIBC23_COMPLEX_UNARY
#ifdef __cplusplus
}
#endif
#endif
