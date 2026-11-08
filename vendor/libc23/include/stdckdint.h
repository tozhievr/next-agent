/* <stdckdint.h> - C23 7.20 checked integer arithmetic. */
#ifndef _LIBC23_STDCKDINT_H
#define _LIBC23_STDCKDINT_H

#define __STDC_VERSION_STDCKDINT_H__ 202311L

/* Clang's overflow builtins compute the exact mathematical operation, store
 * its conversion to the result type, and report whether that type represents
 * the exact result (C23 7.20.1). */
#define ckd_add(r, a, b) __builtin_add_overflow((a), (b), (r))
#define ckd_sub(r, a, b) __builtin_sub_overflow((a), (b), (r))
#define ckd_mul(r, a, b) __builtin_mul_overflow((a), (b), (r))

#endif
