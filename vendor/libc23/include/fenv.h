/* <fenv.h> - C23 7.6 (libc23) */
#ifndef _LIBC23_FENV_H
#define _LIBC23_FENV_H

#define __STDC_VERSION_FENV_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__)

/* exception flags: same bit positions as MXCSR / x87 status word */
#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_ALL_EXCEPT 0x3d

/* rounding: x87 control word bits 10-11 */
#define FE_TONEAREST  0x000
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xc00

typedef struct {
    unsigned int __x87[7];   /* fnstenv image (zero on platforms without x87 long double) */
    unsigned int __mxcsr;
} fenv_t;
typedef struct {
    unsigned int   __mxcsr;
    unsigned short __x87cw;
    unsigned short __pad;
} femode_t;

#elif defined(__aarch64__)

#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x02
#define FE_OVERFLOW   0x04
#define FE_UNDERFLOW  0x08
#define FE_INEXACT    0x10
#define FE_ALL_EXCEPT 0x1f

#define FE_TONEAREST  0x000000
#define FE_UPWARD     0x400000
#define FE_DOWNWARD   0x800000
#define FE_TOWARDZERO 0xc00000

typedef struct {
    unsigned int __fpcr;
    unsigned int __fpsr;
} fenv_t;
typedef struct {
    unsigned int __fpcr;
} femode_t;

#else
#error "libc23 <fenv.h>: unsupported architecture"
#endif

typedef unsigned int fexcept_t;

#define FE_DFL_ENV  ((const fenv_t *)-1)
#define FE_DFL_MODE ((const femode_t *)-1)

int feclearexcept(int __excepts);
int fegetexceptflag(fexcept_t *__flagp, int __excepts);
int feraiseexcept(int __excepts);
int fesetexcept(int __excepts);
int fesetexceptflag(const fexcept_t *__flagp, int __excepts);
int fetestexceptflag(const fexcept_t *__flagp, int __excepts);
int fetestexcept(int __excepts);

int fegetround(void);
int fesetround(int __round);

int fegetmode(femode_t *__modep);
int fesetmode(const femode_t *__modep);

int fegetenv(fenv_t *__envp);
int feholdexcept(fenv_t *__envp);
int fesetenv(const fenv_t *__envp);
int feupdateenv(const fenv_t *__envp);

#ifdef __cplusplus
}
#endif
#endif
