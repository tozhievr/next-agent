/* wcstol wcstoll wcstoul wcstoull (C23 7.31.4.2, incl. 0b/0B binary prefix) */
#include <wchar.h>
#include <limits.h>
#include "wchar_impl.h"

int __libc_wc_isspace(uint32_t c) {
    if (c == ' ' || (c >= 9 && c <= 13)) return 1;
    if (!__libc_locale_utf8 || c < 0x80) return 0;
    return c == 0x85 || c == 0x1680 || (c >= 0x2000 && c <= 0x2006) ||
           (c >= 0x2008 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x205F ||
           c == 0x3000;
}

static int digit_val(wchar_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* Returns the magnitude (saturated to ULLONG_MAX on overflow of 64 bits) and
 * sets *neg / *ovf (ovf: magnitude exceeded `limit`).  *end gets the end pointer. */
static unsigned long long conv(const wchar_t *s, wchar_t **end, int base, int *neg, int *ovf,
                               unsigned long long limit_pos, unsigned long long limit_neg) {
    const wchar_t *p = s;
    *neg = 0; *ovf = 0;
    if (base != 0 && (base < 2 || base > 36)) {
        if (end) *end = (wchar_t *)s;
        errno = EINVAL;
        return 0;
    }
    while (__libc_wc_isspace((uint32_t)*p)) p++;
    if (*p == '+') p++;
    else if (*p == '-') { *neg = 1; p++; }

    if (*p == '0' && (base == 0 || base == 16 || base == 2)) {
        wchar_t x = p[1];
        int want16 = (x == 'x' || x == 'X') && (base == 0 || base == 16);
        int want2  = (x == 'b' || x == 'B') && (base == 0 || base == 2);
        if ((want16 && digit_val(p[2]) < 16) || (want2 && digit_val(p[2]) < 2)) {
            base = want16 ? 16 : 2;
            p += 2;
        } else if (base == 0) {
            base = 8;
        }
    } else if (base == 0) {
        base = (*p == '0') ? 8 : 10;
    }

    unsigned long long limit = *neg ? limit_neg : limit_pos;
    unsigned long long v = 0, cutoff = limit / (unsigned)base;
    unsigned cutlim = (unsigned)(limit % (unsigned)base);
    int any = 0;
    for (;; p++) {
        int d = digit_val(*p);
        if (d >= base) break;
        any = 1;
        if (*ovf) continue;
        if (v > cutoff || (v == cutoff && (unsigned)d > cutlim)) { *ovf = 1; continue; }
        v = v * (unsigned)base + (unsigned)d;
    }
    if (!any) {
        if (end) *end = (wchar_t *)s;
        return 0;
    }
    if (end) *end = (wchar_t *)p;
    return v;
}

static long long sconv(const wchar_t *restrict s, wchar_t **restrict end, int base,
                       long long minv, long long maxv) {
    int neg, ovf;
    unsigned long long limit_neg = 0ull - (unsigned long long)minv;
    unsigned long long v = conv(s, end, base, &neg, &ovf, (unsigned long long)maxv, limit_neg);
    if (ovf) { errno = ERANGE; return neg ? minv : maxv; }
    if (!neg) return (long long)v;
    if (v == limit_neg) return minv;
    return -(long long)v;
}

static unsigned long long uconv(const wchar_t *restrict s, wchar_t **restrict end, int base,
                                unsigned long long maxv) {
    int neg, ovf;
    unsigned long long v = conv(s, end, base, &neg, &ovf, maxv, maxv);
    if (ovf) { errno = ERANGE; return maxv; }
    return neg ? (0ull - v) & maxv : v;
}

long wcstol(const wchar_t *restrict s, wchar_t **restrict e, int b) {
    return (long)sconv(s, e, b, LONG_MIN, LONG_MAX);
}
long long wcstoll(const wchar_t *restrict s, wchar_t **restrict e, int b) {
    return sconv(s, e, b, LLONG_MIN, LLONG_MAX);
}
unsigned long wcstoul(const wchar_t *restrict s, wchar_t **restrict e, int b) {
    return (unsigned long)uconv(s, e, b, ULONG_MAX);
}
unsigned long long wcstoull(const wchar_t *restrict s, wchar_t **restrict e, int b) {
    return uconv(s, e, b, ULLONG_MAX);
}
