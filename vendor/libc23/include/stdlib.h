/* <stdlib.h> - C23 7.24 (libc23) */
#ifndef _LIBC23_STDLIB_H
#define _LIBC23_STDLIB_H

#define __STDC_VERSION_STDLIB_H__ 202311L

#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

/* rand() returns values in [0, RAND_MAX] */
#define RAND_MAX 0x7fffffff

size_t __ctype_get_mb_cur_max(void);
#define MB_CUR_MAX (__ctype_get_mb_cur_max())

/* 7.24.1 numeric conversion */
double atof(const char *nptr);
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
int strfromd(char *restrict s, size_t n, const char *restrict format, double fp);
int strfromf(char *restrict s, size_t n, const char *restrict format, float fp);
int strfroml(char *restrict s, size_t n, const char *restrict format, long double fp);
double strtod(const char *restrict nptr, char **restrict endptr);
float strtof(const char *restrict nptr, char **restrict endptr);
long double strtold(const char *restrict nptr, char **restrict endptr);
long strtol(const char *restrict nptr, char **restrict endptr, int base);
long long strtoll(const char *restrict nptr, char **restrict endptr, int base);
unsigned long strtoul(const char *restrict nptr, char **restrict endptr, int base);
unsigned long long strtoull(const char *restrict nptr, char **restrict endptr, int base);

/* 7.24.2 pseudo-random sequence */
int rand(void);
void srand(unsigned int seed);

/* 7.24.3 memory management */
void *aligned_alloc(size_t alignment, size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void free_sized(void *ptr, size_t size);
void free_aligned_sized(void *ptr, size_t alignment, size_t size);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);

/* 7.24.4 communication with the environment */
[[noreturn]] void abort(void);
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
[[noreturn]] void exit(int status);
[[noreturn]] void _Exit(int status);
char *getenv(const char *name);
[[noreturn]] void quick_exit(int status);
int system(const char *string);

/* 7.24.5 searching and sorting */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

/* 7.24.6 integer arithmetic */
int abs(int j);
long labs(long j);
long long llabs(long long j);
div_t div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

/* 7.24.7 multibyte / wide character conversion */
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n);
int wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *restrict pwcs, const char *restrict s, size_t n);
size_t wcstombs(char *restrict s, const wchar_t *restrict pwcs, size_t n);

/* C23 7.24.9.1 */
size_t memalignment(const void *p);

#ifdef __cplusplus
}
#endif

/* C23 preserves const qualification in the returned void pointer.  The
 * conditional expression forms the composite pointer type without evaluating
 * base; the selected call below evaluates each argument once. */
#if !defined(__cplusplus) && !defined(_LIBC23_BUILD) && !defined(_LIBC23_NO_QUAL_GENERIC)
#define bsearch(key, base, nmemb, size, compar) \
    _Generic((1 ? (base) : (void *)1), \
        const void *: (const void *)(bsearch)((key), (base), (nmemb), (size), (compar)), \
        const volatile void *: (const void *)(bsearch)((key), (base), (nmemb), (size), (compar)), \
        default: (void *)(bsearch)((key), (base), (nmemb), (size), (compar)))
#endif
#endif /* _LIBC23_STDLIB_H */
