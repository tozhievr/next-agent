/* Little-endian binary encoding for the on-disk caches: a writer on Buf and a bounds-checked reader
 * (a truncated or corrupted cache sets `bad` and the caller just throws the cache away). */
#ifndef NA_BIN_H
#define NA_BIN_H
#include "util.h"
#include <string.h>

static inline void bin_u32(Buf *b, uint32_t v) {
    unsigned char c[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    buf_put(b, c, 4);
}

static inline void bin_u64(Buf *b, uint64_t v) {
    bin_u32(b, (uint32_t)v);
    bin_u32(b, (uint32_t)(v >> 32));
}

/* NULL is stored as length 0xFFFFFFFF, so it round-trips and differs from "" */
static inline void bin_str(Buf *b, const char *s) {
    if (!s) {
        bin_u32(b, 0xFFFFFFFFu);
        return;
    }
    size_t n = strlen(s);
    bin_u32(b, (uint32_t)n);
    buf_put(b, s, n);
}

static inline void bin_f32s(Buf *b, const float *f, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t u;
        memcpy(&u, &f[i], 4);
        bin_u32(b, u);
    }
}

typedef struct {
    const unsigned char *p;
    size_t n, i;
    bool bad;
} BinR;

static inline bool binr_room(BinR *r, size_t count, size_t elem) {
    if (r->bad || (elem && count > (r->n - r->i) / elem)) {
        r->bad = true;
        return false;
    }
    return true;
}

static inline uint32_t binr_u32(BinR *r) {
    if (!binr_room(r, 4, 1)) return 0;
    const unsigned char *c = r->p + r->i;
    r->i += 4;
    return (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
}

static inline uint64_t binr_u64(BinR *r) {
    uint64_t lo = binr_u32(r);
    return lo | (uint64_t)binr_u32(r) << 32;
}

/* owned copy, NULL for a stored NULL (and on error: check r->bad) */
static inline char *binr_str(BinR *r) {
    uint32_t n = binr_u32(r);
    if (n == 0xFFFFFFFFu || !binr_room(r, n, 1)) return NULL;
    char *s = na_strndup((const char *)r->p + r->i, n);
    r->i += n;
    return s;
}

static inline void binr_f32s(BinR *r, float *out, size_t n) {
    if (!binr_room(r, n, 4)) return;
    for (size_t i = 0; i < n; i++) {
        uint32_t u = binr_u32(r);
        memcpy(&out[i], &u, 4);
    }
}

#endif
