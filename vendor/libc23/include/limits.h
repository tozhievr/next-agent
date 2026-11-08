/* C23 integer type limits for Clang targets supported by libc23. */
#ifndef _LIBC23_LIMITS_H
#define _LIBC23_LIMITS_H
#define __STDC_VERSION_LIMITS_H__ 202311L
#define CHAR_BIT __CHAR_BIT__
#define SCHAR_MAX __SCHAR_MAX__
#define SCHAR_MIN (-SCHAR_MAX - 1)
#define UCHAR_MAX (SCHAR_MAX * 2 + 1)
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif
#define SHRT_MAX __SHRT_MAX__
#define SHRT_MIN (-SHRT_MAX - 1)
#if __SHRT_WIDTH__ < __INT_WIDTH__
#define USHRT_MAX (SHRT_MAX * 2 + 1)
#else
#define USHRT_MAX (SHRT_MAX * 2U + 1U)
#endif
#define INT_MAX __INT_MAX__
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX (INT_MAX * 2U + 1U)
#define LONG_MAX __LONG_MAX__
#define LONG_MIN (-LONG_MAX - 1L)
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#define MB_LEN_MAX 4
#define BOOL_WIDTH __BOOL_WIDTH__
#define BOOL_MAX 1
#define CHAR_WIDTH __CHAR_BIT__
#define SCHAR_WIDTH __CHAR_BIT__
#define UCHAR_WIDTH __CHAR_BIT__
#define SHRT_WIDTH __SHRT_WIDTH__
#define USHRT_WIDTH __SHRT_WIDTH__
#define INT_WIDTH __INT_WIDTH__
#define UINT_WIDTH __INT_WIDTH__
#define LONG_WIDTH __LONG_WIDTH__
#define ULONG_WIDTH __LONG_WIDTH__
#define LLONG_WIDTH __LLONG_WIDTH__
#define ULLONG_WIDTH __LLONG_WIDTH__
#define BITINT_MAXWIDTH __BITINT_MAXWIDTH__
#endif

