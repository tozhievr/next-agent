/* atoi atol atoll atof strtol strtoll strtoul strtoull (C23 7.24.1) */
#include "libc.h"
#include <stdlib.h>

static int is_space(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static int digit_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* Core: parses magnitude into *mag (saturating at ULLONG_MAX, *ovf set).
 * Returns 0 and sets *end = s if no conversion could be performed. */
static void conv(const char *s, char **end, int base, unsigned long long *mag, int *neg, int *ovf) {
    const char *p = s;
    *mag = 0; *neg = 0; *ovf = 0;
    if (base != 0 && (base < 2 || base > 36)) {
        errno = EINVAL;
        if (end) *end = (char *)s;
        return;
    }
    while (is_space((unsigned char)*p)) p++;
    if (*p == '+') p++;
    else if (*p == '-') { *neg = 1; p++; }

    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val((unsigned char)p[2]) < 16) {
        p += 2; base = 16;
    } else if ((base == 0 || base == 2) && p[0] == '0' && (p[1] == 'b' || p[1] == 'B') && (p[2] == '0' || p[2] == '1')) {
        p += 2; base = 2;
    } else if (base == 0) {
        base = (p[0] == '0') ? 8 : 10;
    }

    const char *start = p;
    unsigned long long acc = 0;
    unsigned long long cutoff = ULLONG_MAX / (unsigned)base;
    unsigned cutlim = (unsigned)(ULLONG_MAX % (unsigned)base);
    for (;; p++) {
        int d = digit_val((unsigned char)*p);
        if (d >= base) break;
        if (*ovf) continue;
        if (acc > cutoff || (acc == cutoff && (unsigned)d > cutlim)) { *ovf = 1; acc = ULLONG_MAX; }
        else acc = acc * (unsigned)base + (unsigned)d;
    }
    if (p == start) { /* no digits */
        if (end) *end = (char *)s;
        *neg = 0;
        return;
    }
    *mag = acc;
    if (end) *end = (char *)p;
}

static long long conv_signed(const char *s, char **end, int base, long long maxv, long long minv) {
    unsigned long long mag; int neg, ovf;
    conv(s, end, base, &mag, &neg, &ovf);
    if (neg) {
        if (ovf || mag > (unsigned long long)maxv + 1u) { errno = ERANGE; return minv; }
        if (mag == (unsigned long long)maxv + 1u) return minv;
        return -(long long)mag;
    }
    if (ovf || mag > (unsigned long long)maxv) { errno = ERANGE; return maxv; }
    return (long long)mag;
}

static unsigned long long conv_unsigned(const char *s, char **end, int base, unsigned long long maxv) {
    unsigned long long mag; int neg, ovf;
    conv(s, end, base, &mag, &neg, &ovf);
    if (ovf || mag > maxv) { errno = ERANGE; return maxv; }
    return neg ? (0ull - mag) & maxv : mag;
}

long long strtoll(const char *restrict s, char **restrict end, int base) {
    return conv_signed(s, end, base, LLONG_MAX, LLONG_MIN);
}
long strtol(const char *restrict s, char **restrict end, int base) {
    return (long)conv_signed(s, end, base, LONG_MAX, LONG_MIN);
}
unsigned long long strtoull(const char *restrict s, char **restrict end, int base) {
    return conv_unsigned(s, end, base, ULLONG_MAX);
}
unsigned long strtoul(const char *restrict s, char **restrict end, int base) {
    return (unsigned long)conv_unsigned(s, end, base, ULONG_MAX);
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }
double atof(const char *s) { return strtod(s, NULL); }
