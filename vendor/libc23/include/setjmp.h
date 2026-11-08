/* <setjmp.h> - C23 7.13 (libc23) */
#ifndef _LIBC23_SETJMP_H
#define _LIBC23_SETJMP_H

#define __STDC_VERSION_SETJMP_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

/* 40 * 8 = 320 bytes, 16-byte aligned (room for xmm6-xmm15 on Windows x64,
 * x19-x30/sp/d8-d15 on aarch64).  The layout is private to arch/<arch>/setjmp.c. */
typedef struct __jmp_buf_tag {
    unsigned long long __b[40];
} __attribute__((aligned(16))) jmp_buf[1];

int setjmp(jmp_buf __env) __attribute__((returns_twice));
#define setjmp(env) setjmp(env)

[[noreturn]] void longjmp(jmp_buf __env, int __val);

#ifdef __cplusplus
}
#endif
#endif
