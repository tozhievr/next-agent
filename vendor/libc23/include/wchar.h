/* <wchar.h> - C23 7.31 (libc23). */
#ifndef _LIBC23_WCHAR_H
#define _LIBC23_WCHAR_H

#define __STDC_VERSION_WCHAR_H__ 202311L

#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#define __need_wint_t
#include <stddef.h>
#define __need___va_list
#include <stdarg.h>
#include <bits/mbstate.h>

#if __WCHAR_MAX__ < 0x10FFFF
#error "libc23 requires a 32-bit wchar_t: compile with -fno-short-wchar"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __FILE FILE;
struct tm;

#ifndef WCHAR_MAX
#define WCHAR_MAX __WCHAR_MAX__
#endif
#ifndef WCHAR_MIN
#define WCHAR_MIN ((wchar_t)-1 < 0 ? (-__WCHAR_MAX__ - 1) : 0)
#endif
#ifndef WEOF
#define WEOF 0xFFFFFFFFu
#endif

/* formatted wide I/O */
int fwprintf(FILE *restrict stream, const wchar_t *restrict format, ...);
int fwscanf(FILE *restrict stream, const wchar_t *restrict format, ...);
int swprintf(wchar_t *restrict s, size_t n, const wchar_t *restrict format, ...);
int swscanf(const wchar_t *restrict s, const wchar_t *restrict format, ...);
int vfwprintf(FILE *restrict stream, const wchar_t *restrict format, __gnuc_va_list arg);
int vfwscanf(FILE *restrict stream, const wchar_t *restrict format, __gnuc_va_list arg);
int vswprintf(wchar_t *restrict s, size_t n, const wchar_t *restrict format, __gnuc_va_list arg);
int vswscanf(const wchar_t *restrict s, const wchar_t *restrict format, __gnuc_va_list arg);
int vwprintf(const wchar_t *restrict format, __gnuc_va_list arg);
int vwscanf(const wchar_t *restrict format, __gnuc_va_list arg);
int wprintf(const wchar_t *restrict format, ...);
int wscanf(const wchar_t *restrict format, ...);

/* wide character I/O */
wint_t fgetwc(FILE *stream);
wchar_t *fgetws(wchar_t *restrict s, int n, FILE *restrict stream);
wint_t fputwc(wchar_t c, FILE *stream);
int fputws(const wchar_t *restrict s, FILE *restrict stream);
int fwide(FILE *stream, int mode);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t putwc(wchar_t c, FILE *stream);
wint_t putwchar(wchar_t c);
wint_t ungetwc(wint_t c, FILE *stream);

/* numeric conversion */
double wcstod(const wchar_t *restrict nptr, wchar_t **restrict endptr);
float wcstof(const wchar_t *restrict nptr, wchar_t **restrict endptr);
long double wcstold(const wchar_t *restrict nptr, wchar_t **restrict endptr);
long int wcstol(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
long long int wcstoll(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
unsigned long int wcstoul(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
unsigned long long int wcstoull(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);

/* string / array utilities */
wchar_t *wcscpy(wchar_t *restrict s1, const wchar_t *restrict s2);
wchar_t *wcsncpy(wchar_t *restrict s1, const wchar_t *restrict s2, size_t n);
wchar_t *wmemcpy(wchar_t *restrict s1, const wchar_t *restrict s2, size_t n);
wchar_t *wmemmove(wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcscat(wchar_t *restrict s1, const wchar_t *restrict s2);
wchar_t *wcsncat(wchar_t *restrict s1, const wchar_t *restrict s2, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcscoll(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
size_t wcsxfrm(wchar_t *restrict s1, const wchar_t *restrict s2, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
size_t wcscspn(const wchar_t *s1, const wchar_t *s2);
size_t wcsspn(const wchar_t *s1, const wchar_t *s2);
size_t wcslen(const wchar_t *s);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
wchar_t *wcstok(wchar_t *restrict s1, const wchar_t *restrict s2, wchar_t **restrict ptr);

/* C23 type-generic search functions: qualifier-preserving. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ > 201710L && !defined(__cplusplus)
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcspbrk(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *s1, const wchar_t *s2);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
#if defined(__clang__) && !defined(_LIBC23_NO_QUAL_GENERIC)
/* Return const-qualified results for const-qualified arguments (C23 7.31.4.6). */
#define wcschr(s, c)    _Generic((s), const wchar_t *: (const wchar_t *)(wcschr)((s), (c)), default: (wcschr)((s), (c)))
#define wcspbrk(s, p)   _Generic((s), const wchar_t *: (const wchar_t *)(wcspbrk)((s), (p)), default: (wcspbrk)((s), (p)))
#define wcsrchr(s, c)   _Generic((s), const wchar_t *: (const wchar_t *)(wcsrchr)((s), (c)), default: (wcsrchr)((s), (c)))
#define wcsstr(s, p)    _Generic((s), const wchar_t *: (const wchar_t *)(wcsstr)((s), (p)), default: (wcsstr)((s), (p)))
#define wmemchr(s, c, n) _Generic((s), const wchar_t *: (const wchar_t *)(wmemchr)((s), (c), (n)), default: (wmemchr)((s), (c), (n)))
#endif
#else
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcspbrk(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *s1, const wchar_t *s2);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
#endif

size_t wcsftime(wchar_t *restrict s, size_t maxsize, const wchar_t *restrict format,
                const struct tm *restrict timeptr);

/* multibyte / wide character conversion */
wint_t btowc(int c);
int wctob(wint_t c);
int mbsinit(const mbstate_t *ps);
size_t mbrlen(const char *restrict s, size_t n, mbstate_t *restrict ps);
size_t mbrtowc(wchar_t *restrict pwc, const char *restrict s, size_t n, mbstate_t *restrict ps);
size_t wcrtomb(char *restrict s, wchar_t wc, mbstate_t *restrict ps);
size_t mbsrtowcs(wchar_t *restrict dst, const char **restrict src, size_t len, mbstate_t *restrict ps);
size_t wcsrtombs(char *restrict dst, const wchar_t **restrict src, size_t len, mbstate_t *restrict ps);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_WCHAR_H */
