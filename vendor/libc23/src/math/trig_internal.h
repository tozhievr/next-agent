#ifndef LIBC23_TRIG_INTERNAL_H
#define LIBC23_TRIG_INTERNAL_H
#include "libm.h"
#define GET_HIGH_WORD(i, x) ((i) = (uint32_t)(asuint64(x) >> 32))
#define SET_LOW_WORD(x, i) ((x) = asdouble((asuint64(x) & 0xffffffff00000000ULL) | (uint32_t)(i)))
#define FORCE_EVAL(x) fp_force_eval_d(x)
#define predict_false(x) __builtin_expect(!!(x), 0)
double __sin(double, double, int);
double __cos(double, double);
double __tan(double, double, int);
int __rem_pio2(double, double *);
int __rem_pio2_large(double *, double *, int, int, int);
#endif
