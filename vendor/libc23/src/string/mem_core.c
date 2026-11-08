/* memcpy / memmove / memset / memcmp.
 * The compiler also emits calls to these; the code below only uses
 * constant-size __builtin_memcpy (always inlined) so it cannot recurse. */
#include <stddef.h>
#include <stdint.h>

#define LD(T, p) ({ T v_; __builtin_memcpy(&v_, (p), sizeof v_); v_; })
#define ST(T, p, v) do { T v_ = (v); __builtin_memcpy((p), &v_, sizeof v_); } while (0)

typedef struct { unsigned char b[16]; } blk16;

/* copy 0..32 bytes; all loads happen before any store (overlap safe) */
static inline void copy_small(unsigned char *d, const unsigned char *s, size_t n) {
    if (n >= 16) {
        blk16 a = LD(blk16, s), b = LD(blk16, s + n - 16);
        ST(blk16, d, a); ST(blk16, d + n - 16, b);
    } else if (n >= 8) {
        uint64_t a = LD(uint64_t, s), b = LD(uint64_t, s + n - 8);
        ST(uint64_t, d, a); ST(uint64_t, d + n - 8, b);
    } else if (n >= 4) {
        uint32_t a = LD(uint32_t, s), b = LD(uint32_t, s + n - 4);
        ST(uint32_t, d, a); ST(uint32_t, d + n - 4, b);
    } else if (n >= 2) {
        uint16_t a = LD(uint16_t, s), b = LD(uint16_t, s + n - 2);
        ST(uint16_t, d, a); ST(uint16_t, d + n - 2, b);
    } else if (n) {
        *d = *s;
    }
}

/* forward copy of n > 32 bytes (safe when d <= s or regions are disjoint) */
static inline void copy_forward(unsigned char *d, const unsigned char *s, size_t n) {
#if defined(__x86_64__)
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
#else
    uint64_t tail = LD(uint64_t, s + n - 8);
    unsigned char *dend = d + n - 8;
    while (d < dend) { ST(uint64_t, d, LD(uint64_t, s)); d += 8; s += 8; }
    ST(uint64_t, dend, tail);
#endif
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    if (n <= 32) copy_small(dst, src, n);
    else copy_forward(dst, src, n);
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    if (d == s || n == 0) return dst;
    if (n <= 32) { copy_small(d, s, n); return dst; }
    if ((uintptr_t)d - (uintptr_t)s >= n) {      /* d < s or no overlap: forward */
        copy_forward(d, s, n);
    } else {                                      /* overlapping with d > s: backward */
        uint64_t head = LD(uint64_t, s);
        d += n; s += n;
        size_t pos = n;
        while (pos > 8) {
            pos -= 8;
            ST(uint64_t, d - n + pos, LD(uint64_t, s - n + pos));
        }
        ST(uint64_t, d - n, head);
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
#if defined(__x86_64__)
    if (n >= 128) {
        __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
        return dst;
    }
#endif
    uint64_t p = (unsigned char)c * 0x0101010101010101ull;
    if (n >= 16) {
        unsigned char *end = d + n;
        ST(uint64_t, d, p); ST(uint64_t, d + 8, p);
        ST(uint64_t, end - 16, p); ST(uint64_t, end - 8, p);
        for (unsigned char *q = d + 16; (size_t)(end - q) > 16; q += 8) ST(uint64_t, q, p);
    } else if (n >= 8) {
        ST(uint64_t, d, p); ST(uint64_t, d + n - 8, p);
    } else if (n >= 4) {
        ST(uint32_t, d, (uint32_t)p); ST(uint32_t, d + n - 4, (uint32_t)p);
    } else if (n >= 2) {
        ST(uint16_t, d, (uint16_t)p); ST(uint16_t, d + n - 2, (uint16_t)p);
    } else if (n) {
        *d = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    while (n >= 8 && LD(uint64_t, x) == LD(uint64_t, y)) { x += 8; y += 8; n -= 8; }
    for (; n; n--, x++, y++)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
