/* wcstod wcstof wcstold: find the longest floating-point subject sequence,
 * narrow it (it is pure ASCII) and delegate the conversion to strtod & co. */
#include <wchar.h>
#include "wchar_impl.h"

double strtod(const char *restrict, char **restrict);
float strtof(const char *restrict, char **restrict);
long double strtold(const char *restrict, char **restrict);
void *malloc(size_t);
void free(void *);

static int lower(wchar_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int is_dig(wchar_t c) { return c >= '0' && c <= '9'; }
static int is_xdig(wchar_t c) { return is_dig(c) || (lower(c) >= 'a' && lower(c) <= 'f'); }

static int match(const wchar_t *p, const char *lit) {
    for (; *lit; p++, lit++)
        if (lower(*p) != *lit) return 0;
    return 1;
}

/* Length of the longest valid subject starting at p (after whitespace): 0 if none. */
static size_t subject_len(const wchar_t *p) {
    const wchar_t *s = p;
    if (*p == '+' || *p == '-') p++;
    if (match(p, "infinity")) return (size_t)(p + 8 - s);
    if (match(p, "inf")) return (size_t)(p + 3 - s);
    if (match(p, "nan")) {
        p += 3;
        if (*p == '(') {
            const wchar_t *q = p + 1;
            while (is_dig(*q) || (lower(*q) >= 'a' && lower(*q) <= 'z') || *q == '_') q++;
            if (*q == ')') p = q + 1;
        }
        return (size_t)(p - s);
    }
    int hex = 0;
    if (p[0] == '0' && lower(p[1]) == 'x' && (is_xdig(p[2]) || (p[2] == '.' && is_xdig(p[3])))) {
        hex = 1;
        p += 2;
    }
    size_t digits = 0;
    while (hex ? is_xdig(*p) : is_dig(*p)) { p++; digits++; }
    if (*p == '.') {
        const wchar_t *q = p + 1;
        size_t fd = 0;
        while (hex ? is_xdig(*q) : is_dig(*q)) { q++; fd++; }
        if (digits || fd) { p = q; digits += fd; }
    }
    if (!digits) return 0;
    wchar_t e = lower(*p);
    if (hex ? e == 'p' : e == 'e') {
        const wchar_t *q = p + 1;
        if (*q == '+' || *q == '-') q++;
        if (is_dig(*q)) {
            while (is_dig(*q)) q++;
            p = q;
        }
    }
    return (size_t)(p - s);
}

enum { KIND_D, KIND_F, KIND_LD };

static void conv(const wchar_t *restrict nptr, wchar_t **restrict endptr, int kind, void *out) {
    const wchar_t *p = nptr;
    while (__libc_wc_isspace((uint32_t)*p)) p++;
    size_t len = subject_len(p);
    double d = 0; float f = 0; long double ld = 0;
    size_t used = 0;
    if (len) {
        char stack[256];
        char *buf = stack;
        const wchar_t *token = p;
        if (*token == '+' || *token == '-') ++token;
        int special = match(token, "inf") || match(token, "nan");
        /* Payload text cannot affect the chosen NaN representation here.
         * Avoid allocating in proportion to an arbitrarily long payload. */
        size_t copied = special ? (size_t)(token - p) + 3 : len;
        if (copied + 1 > sizeof stack) {
            buf = malloc(copied + 1);
            if (!buf) {
                /* A truncated subject can change both its value and endptr. */
                if (endptr) *endptr = (wchar_t *)nptr;
                switch (kind) {
                case KIND_D: *(double *)out = 0; break;
                case KIND_F: *(float *)out = 0; break;
                default: *(long double *)out = 0; break;
                }
                return;
            }
        }
        for (size_t i = 0; i < copied; i++) buf[i] = (char)p[i];
        buf[copied] = 0;
        char *e;
        switch (kind) {
        case KIND_D:  d = strtod(buf, &e); break;
        case KIND_F:  f = strtof(buf, &e); break;
        default:      ld = strtold(buf, &e); break;
        }
        used = special ? len : (size_t)(e - buf);
        if (buf != stack) free(buf);
    }
    if (endptr) *endptr = (wchar_t *)(used ? p + used : nptr);
    switch (kind) {
    case KIND_D:  *(double *)out = d; break;
    case KIND_F:  *(float *)out = f; break;
    default:      *(long double *)out = ld; break;
    }
}

double wcstod(const wchar_t *restrict nptr, wchar_t **restrict endptr) {
    double r; conv(nptr, endptr, KIND_D, &r); return r;
}
float wcstof(const wchar_t *restrict nptr, wchar_t **restrict endptr) {
    float r; conv(nptr, endptr, KIND_F, &r); return r;
}
long double wcstold(const wchar_t *restrict nptr, wchar_t **restrict endptr) {
    long double r; conv(nptr, endptr, KIND_LD, &r); return r;
}
