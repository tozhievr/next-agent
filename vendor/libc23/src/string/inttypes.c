/* <inttypes.h> functions.  intmax_t is long long on all libc23 targets. */
#include <inttypes.h>

long long strtoll(const char *restrict, char **restrict, int);
unsigned long long strtoull(const char *restrict, char **restrict, int);
long long wcstoll(const wchar_t *restrict, wchar_t **restrict, int);
unsigned long long wcstoull(const wchar_t *restrict, wchar_t **restrict, int);

_Static_assert(sizeof(intmax_t) == sizeof(long long), "intmax_t must be long long");

intmax_t imaxabs(intmax_t j) { return j < 0 ? (intmax_t)(0 - (uintmax_t)j) : j; }

imaxdiv_t imaxdiv(intmax_t n, intmax_t d) {
    imaxdiv_t r = { n / d, n % d };
    return r;
}

intmax_t strtoimax(const char *restrict s, char **restrict e, int b) { return strtoll(s, e, b); }
uintmax_t strtoumax(const char *restrict s, char **restrict e, int b) { return strtoull(s, e, b); }
intmax_t wcstoimax(const wchar_t *restrict s, wchar_t **restrict e, int b) { return wcstoll(s, e, b); }
uintmax_t wcstoumax(const wchar_t *restrict s, wchar_t **restrict e, int b) { return wcstoull(s, e, b); }
