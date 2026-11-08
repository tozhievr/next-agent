/* Exact-rational conversions to IEEE binary32 and binary64. */
#include "libc.h"
#include <stdlib.h>
#include <fenv.h>

#define LIMBS 256
#define DIGITS 1200

typedef struct { unsigned n; uint32_t v[LIMBS]; } big;
static void init(big *a, uint32_t x) { a->n = x != 0; if (x) a->v[0] = x; }
static void muladd(big *a, unsigned m, unsigned d) {
    uint64_t carry = d;
    for (unsigned i = 0; i < a->n; ++i) {
        carry += (uint64_t)a->v[i] * m;
        a->v[i] = (uint32_t)carry; carry >>= 32;
    }
    if (carry && a->n < LIMBS) a->v[a->n++] = (uint32_t)carry;
}
static int bits(const big *a) {
    return a->n ? (int)(32 * a->n - __builtin_clz(a->v[a->n - 1])) : 0;
}
static int cmp(const big *a, const big *b) {
    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (unsigned i = a->n; i--;) if (a->v[i] != b->v[i]) return a->v[i] > b->v[i] ? 1 : -1;
    return 0;
}
static void shift(big *a, unsigned k) {
    if (!a->n || !k) return;
    unsigned words = k / 32, rem = k % 32, n = a->n;
    if (words + n + (rem != 0) > LIMBS) return; /* inputs are bounded below */
    if (words) {
        for (unsigned i = n; i--;) a->v[i + words] = a->v[i];
        for (unsigned i = 0; i < words; ++i) a->v[i] = 0;
        a->n += words;
    }
    if (rem) {
        uint32_t carry = 0;
        for (unsigned i = words; i < a->n; ++i) {
            uint32_t next = a->v[i] >> (32 - rem);
            a->v[i] = (a->v[i] << rem) | carry; carry = next;
        }
        if (carry) a->v[a->n++] = carry;
    }
}
static void sub(big *a, const big *b) {
    uint64_t borrow = 0;
    for (unsigned i = 0; i < a->n; ++i) {
        uint64_t x = a->v[i], y = (i < b->n ? b->v[i] : 0) + borrow;
        a->v[i] = (uint32_t)(x - y); borrow = x < y;
    }
    while (a->n && !a->v[a->n - 1]) --a->n;
}
static int isspace_c(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
static int digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c); return c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99;
}
static int match(const char *s, const char *w) {
    while (*w) if (lower((unsigned char)*s++) != *w++) return 0;
    return 1;
}

/* Return the representation directly, avoiding a second floating rounding. */
static uint64_t convert(const char *s, char **end, int precision, int emin, int emax, int bias) {
    const char *p = s;
    int neg = 0, hex = 0, dot = 0, any = 0, seen = 0, kept = 0, sticky = 0;
    /* Far beyond any addressable input, but low enough to combine and
       multiply counters without signed overflow. */
    const int64_t limit = INT64_C(1000000000000000);
    int64_t scale = 0, exponent = 0;
    int expneg = 0;
    unsigned fracbits = (unsigned)precision - 1;
    uint64_t sign, inf = (uint64_t)(2 * bias + 1) << fracbits;
    big a, b, t;
    init(&a, 0); init(&b, 1);
    while (isspace_c((unsigned char)*p)) ++p;
    if (*p == '+' || *p == '-') { neg = *p == '-'; ++p; }
    sign = (uint64_t)neg << (fracbits + (precision == 53 ? 11 : 8));
    if (match(p, "inf")) {
        p += 3; if (match(p, "inity")) p += 5;
        if (end) *end = (char *)p;
        return sign | inf;
    }
    if (match(p, "nan")) {
        p += 3;
        if (*p == '(') {
            const char *q = p + 1;
            while ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') ||
                   (*q >= 'A' && *q <= 'Z') || *q == '_') ++q;
            if (*q == ')') p = q + 1;
        }
        if (end) *end = (char *)p;
        return sign | inf | (UINT64_C(1) << (fracbits - 1));
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (digit((unsigned char)p[2]) < 16 || (p[2] == '.' && digit((unsigned char)p[3]) < 16))) {
        hex = 1; p += 2;
    }
    for (;;) {
        int d = digit((unsigned char)*p), radix = hex ? 16 : 10;
        if (d < radix) {
            any = 1;
            if (d) seen = 1;
            if (seen && kept < DIGITS) {
                muladd(&a, radix, d); ++kept;
                if (dot && scale > -limit) --scale;
            } else if (seen) {
                if (d) sticky = 1;
                if (!dot && scale < limit) ++scale;
            } else if (dot && scale > -limit) --scale;
            ++p;
        } else if (*p == '.' && !dot) { dot = 1; ++p; }
        else break;
    }
    if (!any) { if (end) *end = (char *)s; return 0; }
    if (lower((unsigned char)*p) == (hex ? 'p' : 'e')) {
        const char *q = p + 1;
        if (*q == '+' || *q == '-') { expneg = *q == '-'; ++q; }
        if (*q >= '0' && *q <= '9') {
            do {
                int d = *q - '0';
                if (exponent <= (limit - d) / 10) exponent = exponent * 10 + d;
                else exponent = limit;
                ++q;
            }
            while (*q >= '0' && *q <= '9');
            p = q;
        } else exponent = 0;
    }
    if (end) *end = (char *)p;
    if (!seen) return sign;
    if (hex) scale *= 4;
    scale += expneg ? -exponent : exponent;
    int mode = fegetround(), upward = (mode == FE_UPWARD && !neg) || (mode == FE_DOWNWARD && neg);
    /* The conservative bounds avoid enormous powers while leaving ample room
       for all binary32/binary64 boundaries, including halfway values. */
    int64_t order = hex ? bits(&a) + scale : kept + scale;
    if (order > (hex ? 1100 : 400)) {
        errno = ERANGE; feraiseexcept(FE_OVERFLOW | FE_INEXACT);
        return sign | ((mode == FE_TONEAREST || upward) ? inf : inf - 1);
    }
    if (order < (hex ? -1200 : -400)) {
        errno = ERANGE; feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
        return sign | (upward ? 1 : 0);
    }
    int binexp = (int)scale;
    if (!hex) {
        if (scale >= 0) for (int i = 0; i < scale; ++i) muladd(&a, 5, 0);
        else for (int i = 0; i > scale; --i) muladd(&b, 5, 0);
    }
    int e = bits(&a) - bits(&b) + binexp;
    int delta = bits(&a) - bits(&b);
    if (delta >= 0) { t = b; shift(&t, (unsigned)delta); if (cmp(&a, &t) < 0) --e; }
    else { t = a; shift(&t, (unsigned)-delta); if (cmp(&t, &b) < 0) --e; }
    int target = (e > emin ? e : emin) - (precision - 1);
    int sh = binexp - target;
    if (sh >= 0) shift(&a, (unsigned)sh); else shift(&b, (unsigned)-sh);
    uint64_t q = 0;
    int n = bits(&a) - bits(&b);
    for (int i = n; i >= 0; --i) {
        t = b; shift(&t, (unsigned)i);
        if (cmp(&a, &t) >= 0) { sub(&a, &t); if (i < 64) q |= UINT64_C(1) << i; }
    }
    int inexact = a.n != 0 || sticky;
    int round_up = 0;
    if (inexact && mode == FE_TONEAREST) {
        shift(&a, 1);
        int c = cmp(&a, &b);
        round_up = c > 0 || (c == 0 && (sticky || (q & 1)));
    } else if (inexact && upward) round_up = 1;
    q += round_up;
    if (q >= (UINT64_C(1) << precision)) { q >>= 1; ++e; }
    if (e > emax) {
        errno = ERANGE; feraiseexcept(FE_OVERFLOW | FE_INEXACT);
        return sign | ((mode == FE_TONEAREST || upward) ? inf : inf - 1);
    }
    if (inexact) {
        int tiny = q < (UINT64_C(1) << fracbits);
        if (tiny) { errno = ERANGE; feraiseexcept(FE_UNDERFLOW | FE_INEXACT); }
        else feraiseexcept(FE_INEXACT);
    }
    if (q < (UINT64_C(1) << fracbits)) return sign | q;
    if (e < emin) e = emin;
    return sign | ((uint64_t)(e + bias) << fracbits) | (q & ((UINT64_C(1) << fracbits) - 1));
}

double strtod(const char *restrict s, char **restrict end) {
    union { uint64_t u; double d; } v = { .u = convert(s, end, 53, -1022, 1023, 1023) };
    return v.d;
}
float strtof(const char *restrict s, char **restrict end) {
    union { uint32_t u; float f; } v = { .u = (uint32_t)convert(s, end, 24, -126, 127, 127) };
    return v.f;
}
long double strtold(const char *restrict s, char **restrict end) { return (long double)strtod(s, end); }
