/*
 * fmt_core.h - private interface of the formatted-I/O engines
 * (owner: stdio-format).  Not installed.
 */
#ifndef LIBC23_FMT_CORE_H
#define LIBC23_FMT_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <float.h>
#include "libc.h"

/* printf flags */
#define FMT_LEFT  1u
#define FMT_PLUS  2u
#define FMT_SPACE 4u
#define FMT_ALT   8u
#define FMT_ZERO  16u

/* character-set agnostic emitter used by the floating point formatter */
typedef struct fmt_emitter {
    void *ctx;
    void (*puts)(void *ctx, const char *s, size_t n); /* ASCII text            */
    void (*fill)(void *ctx, char c, size_t n);         /* n copies of ASCII c   */
} fmt_emitter;

/* Decoded floating point value:  |value| = mant * 2^e2  (exact). */
struct fmt_fp {
    int sign;                 /* sign bit                                     */
    int cls;                  /* 0 zero, 1 finite non-zero, 2 inf, 3 nan      */
    int sub;                  /* subnormal                                    */
    int emin;                 /* exponent used by %a for subnormals           */
    unsigned __int128 mant;
    int e2;
};

void __libc_fmt_decode_double(double v, struct fmt_fp *o) LIBC_HIDDEN;
void __libc_fmt_decode_ldouble(long double v, struct fmt_fp *o) LIBC_HIDDEN;

/* Format one floating conversion (conv one of e E f F g G a A) including sign,
 * padding and width.  prec < 0 means "not given". */
void __libc_fmt_float(const fmt_emitter *e, unsigned flags, int width, int prec,
                      int conv, const struct fmt_fp *fp) LIBC_HIDDEN;

/* rounding mode of the FPU: 0 nearest-even, 1 upward, 2 downward, 3 toward zero */
int __libc_fmt_round_mode(void) LIBC_HIDDEN;

/* Locale rule for multibyte <-> wide conversion in this module. */
static inline int fmt_utf8(void) { return __libc_locale_utf8; }

/* Decode one multibyte character. Returns length (>=1), 0 for NUL byte,
 * (size_t)-1 for an invalid sequence, (size_t)-2 if incomplete. */
static inline size_t fmt_mbdecode(const unsigned char *s, size_t n, uint32_t *wc) {
    if (n == 0) return (size_t)-2;
    unsigned c = s[0];
    if (!fmt_utf8() || c < 0x80) { *wc = c; return c ? 1 : 0; }
    size_t len; uint32_t cp, min;
    if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; min = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; min = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; min = 0x10000; }
    else return (size_t)-1;
    for (size_t i = 1; i < len; i++) {
        if (i >= n) return (size_t)-2;
        if ((s[i] & 0xC0) != 0x80) return (size_t)-1;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return (size_t)-1;
    *wc = cp;
    return len;
}

/* Encode wide character; returns length or 0 on failure (EILSEQ). */
static inline size_t fmt_mbencode(uint32_t wc, unsigned char *out) {
    if (!fmt_utf8()) {
        if (wc > 0xFF) return 0;
        out[0] = (unsigned char)wc; return 1;
    }
    if (wc < 0x80) { out[0] = (unsigned char)wc; return 1; }
    if (wc < 0x800) { out[0] = (unsigned char)(0xC0 | (wc >> 6)); out[1] = (unsigned char)(0x80 | (wc & 0x3F)); return 2; }
    if (wc >= 0xD800 && wc <= 0xDFFF) return 0;
    if (wc < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (wc >> 12)); out[1] = (unsigned char)(0x80 | ((wc >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (wc & 0x3F)); return 3;
    }
    if (wc > 0x10FFFF) return 0;
    out[0] = (unsigned char)(0xF0 | (wc >> 18)); out[1] = (unsigned char)(0x80 | ((wc >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((wc >> 6) & 0x3F)); out[3] = (unsigned char)(0x80 | (wc & 0x3F));
    return 4;
}

#endif
