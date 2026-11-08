/*
 * libm.h - internal helpers for src/math and src/complex (not installed).
 * Owner of the file: "math-basic".  Others may only ADD small helpers.
 *
 * Exceptions are raised by performing the arithmetic operation that raises
 * them on volatile operands, so the code works for any rounding mode and
 * regardless of the optimiser (sources are compiled with -frounding-math).
 */
#ifndef LIBC23_LIBM_H
#define LIBC23_LIBM_H

#include <stdint.h>
#include <float.h>
#include <math.h>
#include "libc.h"

static inline uint64_t asuint64(double x) { union { double f; uint64_t i; } u = { x }; return u.i; }
static inline double   asdouble(uint64_t i) { union { uint64_t i; double f; } u = { i }; return u.f; }
static inline uint32_t asuint(float x) { union { float f; uint32_t i; } u = { x }; return u.i; }
static inline float    asfloat(uint32_t i) { union { uint32_t i; float f; } u = { i }; return u.f; }

#define EXTRACT_WORDS(hi, lo, d) do { uint64_t u_ = asuint64(d); (hi) = (uint32_t)(u_ >> 32); (lo) = (uint32_t)u_; } while (0)
#define INSERT_WORDS(d, hi, lo)  do { (d) = asdouble(((uint64_t)(hi) << 32) | (uint32_t)(lo)); } while (0)

/* make sure an expression is evaluated (and its FP flags raised) */
static inline void fp_force_eval_d(double x) { volatile double y; y = x; (void)y; }
static inline void fp_force_eval_f(float x)  { volatile float y;  y = x; (void)y; }

/* ---- error / exception helpers: return the IEEE-754 result and raise flags ---- */
static inline double __math_invalid(double x) {
    volatile double a = x, b = x; double r = (a - b) / (a - b);
    return (x != x) ? x : r;                       /* NaN in -> same NaN; else new NaN, invalid raised */
}
static inline double __math_divzero(uint32_t sign) {
    volatile double z = 0.0; double r = (sign ? -1.0 : 1.0) / z; return r;
}
static inline double __math_oflow(uint32_t sign) {
    volatile double big = DBL_MAX; double r = big * big; return sign ? -r : r;
}
static inline double __math_uflow(uint32_t sign) {
    volatile double tiny = DBL_MIN; double r = tiny * tiny; return sign ? -r : r;
}
static inline double __math_may_uflow(uint32_t sign) { return __math_uflow(sign); }
static inline float __math_invalidf(float x) {
    volatile float a = x, b = x; float r = (a - b) / (a - b);
    return (x != x) ? x : r;
}
static inline float __math_divzerof(uint32_t sign) {
    volatile float z = 0.0f; float r = (sign ? -1.0f : 1.0f) / z; return r;
}
static inline float __math_oflowf(uint32_t sign) {
    volatile float big = FLT_MAX; float r = big * big; return sign ? -r : r;
}
static inline float __math_uflowf(uint32_t sign) {
    volatile float tiny = FLT_MIN; float r = tiny * tiny; return sign ? -r : r;
}

#endif
