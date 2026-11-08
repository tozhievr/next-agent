/* <stddef.h> - C23 fundamental types and definitions.
 *
 * This header intentionally supports repeated partial inclusion through the
 * __need_* requests used by compiler and library headers.  The implementation
 * uses Clang predefined type macros only; it does not include a resource or
 * host libc header.
 */

#if !defined(__need_size_t) && !defined(__need_ptrdiff_t) && \
    !defined(__need_wchar_t) && !defined(__need_NULL) && \
    !defined(__need_wint_t) && !defined(__need_max_align_t) && \
    !defined(__need_offsetof) && !defined(__need_nullptr_t) && \
    !defined(__need_unreachable) && !defined(__need_rsize_t)
#define __LIBC23_STDDEF_FULL_INCLUDE 1
#define __need_size_t 1
#define __need_ptrdiff_t 1
#define __need_wchar_t 1
#define __need_NULL 1
#define __need_max_align_t 1
#define __need_offsetof 1
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define __need_nullptr_t 1
#define __need_unreachable 1
#endif
#endif

#if defined(__need_size_t) && !defined(__LIBC23_SIZE_T_DEFINED)
typedef __SIZE_TYPE__ size_t;
#define __LIBC23_SIZE_T_DEFINED 1
#endif

#if defined(__need_ptrdiff_t) && !defined(__LIBC23_PTRDIFF_T_DEFINED)
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#define __LIBC23_PTRDIFF_T_DEFINED 1
#endif

#if defined(__need_wchar_t) && !defined(__LIBC23_WCHAR_T_DEFINED) && !defined(__cplusplus)
typedef __WCHAR_TYPE__ wchar_t;
#define __LIBC23_WCHAR_T_DEFINED 1
#endif

/* wint_t is requested only by <wchar.h>; this target uses 32-bit wide chars. */
#if defined(__need_wint_t) && !defined(__LIBC23_WINT_T_DEFINED)
typedef unsigned int wint_t;
#define __LIBC23_WINT_T_DEFINED 1
#endif

#if defined(__need_max_align_t) && !defined(__LIBC23_MAX_ALIGN_T_DEFINED)
typedef struct {
    _Alignas(16) unsigned char __force_max_align;
    long long __ll;
    long double __ld;
} max_align_t;
#define __LIBC23_MAX_ALIGN_T_DEFINED 1
#endif

#if defined(__need_NULL) && !defined(NULL)
#if defined(__cplusplus)
#define NULL nullptr
#else
#define NULL ((void *)0)
#endif
#endif

#if defined(__need_offsetof) && !defined(offsetof)
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#if defined(__need_nullptr_t) && !defined(__LIBC23_NULLPTR_T_DEFINED)
#if defined(__cplusplus)
typedef decltype(nullptr) nullptr_t;
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
typedef __typeof__(nullptr) nullptr_t;
#endif
#define __LIBC23_NULLPTR_T_DEFINED 1
#endif

#if defined(__need_unreachable) && !defined(unreachable)
#define unreachable() __builtin_unreachable()
#endif

/* Annex K is not advertised by this implementation, so rsize_t is omitted. */

#ifdef __LIBC23_STDDEF_FULL_INCLUDE
#define __STDC_VERSION_STDDEF_H__ 202311L
#undef __LIBC23_STDDEF_FULL_INCLUDE
#endif

/* Clang's need flags are one-shot requests; consume them like its resource
 * header so later full or partial includes have the expected behavior. */
#undef __need_size_t
#undef __need_ptrdiff_t
#undef __need_wchar_t
#undef __need_NULL
#undef __need_wint_t
#undef __need_max_align_t
#undef __need_offsetof
#undef __need_nullptr_t
#undef __need_unreachable
#undef __need_rsize_t
