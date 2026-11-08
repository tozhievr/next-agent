/* Strict UTF-8 encoder/decoder (RFC 3629): no overlongs, surrogates or > U+10FFFF. */
#include "libc.h"

size_t __libc_utf8_decode(const unsigned char *s, size_t n, uint32_t *cp) {
    /* A zero-length input is a truncated sequence, and must not read s[0]. */
    if (!n) return (size_t)-2;
    unsigned c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if (c < 0xC2 || c > 0xF4) return (size_t)-1;
    size_t need = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    unsigned lo = 0x80, hi = 0xBF;
    if (c == 0xE0) lo = 0xA0;
    else if (c == 0xED) hi = 0x9F;
    else if (c == 0xF0) lo = 0x90;
    else if (c == 0xF4) hi = 0x8F;
    uint32_t v = c & (0x7Fu >> need);
    for (size_t i = 1; i < need; i++) {
        if (i >= n) return (size_t)-2;
        unsigned b = s[i];
        if (i == 1 ? (b < lo || b > hi) : (b < 0x80 || b > 0xBF)) return (size_t)-1;
        v = (v << 6) | (b & 0x3F);
    }
    *cp = v;
    return need;
}

size_t __libc_utf8_encode(uint32_t cp, unsigned char *out) {
    if (cp < 0x80) { out[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (unsigned char)(0xC0 | cp >> 6);
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp < 0x10000) {
        out[0] = (unsigned char)(0xE0 | cp >> 12);
        out[1] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp > 0x10FFFF) return 0;
    out[0] = (unsigned char)(0xF0 | cp >> 18);
    out[1] = (unsigned char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (unsigned char)(0x80 | (cp >> 6 & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}
