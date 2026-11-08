/* memchr, memccpy, memset_explicit */
#include <string.h>
#include <stdint.h>

#define ONES (~(size_t)0 / 255)
#define HIGHS (ONES * 128)

void *memchr(const void *p, int c, size_t n) {
    const unsigned char *s = p;
    unsigned char ch = (unsigned char)c;
    while (n && ((uintptr_t)s & (sizeof(size_t) - 1))) { if (*s == ch) return (void *)s; s++; n--; }
    if (n >= sizeof(size_t)) {
        size_t k = ONES * ch;
        for (; n >= sizeof(size_t); s += sizeof(size_t), n -= sizeof(size_t)) {
            size_t word;
            __builtin_memcpy(&word, s, sizeof word);
            size_t w = word ^ k;
            if ((w - ONES) & ~w & HIGHS) break;
        }
    }
    for (; n; s++, n--) if (*s == ch) return (void *)s;
    return NULL;
}

void *memccpy(void *restrict d, const void *restrict s, int c, size_t n) {
    unsigned char *dp = d; const unsigned char *sp = s;
    unsigned char ch = (unsigned char)c;
    for (; n; n--) {
        unsigned char b = *sp++;
        *dp++ = b;
        if (b == ch) return dp;
    }
    return NULL;
}

void *memset_explicit(void *p, int c, size_t n) {
    volatile unsigned char *d = p;
    unsigned char byte = (unsigned char)c;
    while (n--) *d++ = byte;
    return p;
}
