/* Variadic argument support; types and operations are compiler primitives. */
#if !defined(__need___va_list) && !defined(__need_va_list) && \
    !defined(__need_va_arg) && !defined(__need___va_copy) && !defined(__need_va_copy)
#define __LIBC23_STDARG_FULL 1
#endif

#if defined(__LIBC23_STDARG_FULL) || defined(__need___va_list)
#ifndef __LIBC23_GNUC_VA_LIST_DEFINED
#define __LIBC23_GNUC_VA_LIST_DEFINED
typedef __builtin_va_list __gnuc_va_list;
#endif
#undef __need___va_list
#endif

#if defined(__LIBC23_STDARG_FULL) || defined(__need_va_list)
#ifndef __LIBC23_VA_LIST_DEFINED
#define __LIBC23_VA_LIST_DEFINED
typedef __builtin_va_list va_list;
#endif
#undef __need_va_list
#endif

#if defined(__LIBC23_STDARG_FULL) || defined(__need_va_arg)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define va_start(...) __builtin_c23_va_start(__VA_ARGS__)
#else
#define va_start(ap, last) __builtin_va_start(ap, last)
#endif
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#undef __need_va_arg
#endif

#if defined(__LIBC23_STDARG_FULL) || defined(__need___va_copy)
#define __va_copy(dst, src) __builtin_va_copy(dst, src)
#undef __need___va_copy
#endif

#if defined(__LIBC23_STDARG_FULL) || defined(__need_va_copy)
#define va_copy(dst, src) __builtin_va_copy(dst, src)
#undef __need_va_copy
#endif

#ifdef __LIBC23_STDARG_FULL
#define __STDC_VERSION_STDARG_H__ 202311L
#undef __LIBC23_STDARG_FULL
#endif
