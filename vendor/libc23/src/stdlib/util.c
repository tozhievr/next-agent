/* General-purpose <stdlib.h> utilities. */
#include "libc.h"
#include <stdlib.h>

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int n, int d) { div_t r = { n / d, n % d }; return r; }
ldiv_t ldiv(long n, long d) { ldiv_t r = { n / d, n % d }; return r; }
lldiv_t lldiv(long long n, long long d) { lldiv_t r = { n / d, n % d }; return r; }

static libc_lock_t rand_lock = LIBC_LOCK_INIT;
static unsigned int rand_state = 1;

void srand(unsigned int seed) {
    libc_lock(&rand_lock);
    rand_state = seed;
    libc_unlock(&rand_lock);
}

int rand(void) {
    libc_lock(&rand_lock);
    rand_state = rand_state * 1103515245u + 12345u;
    int result = (int)((rand_state >> 1) & 0x7fffffffu);
    libc_unlock(&rand_lock);
    return result;
}

char *getenv(const char *name) {
    if (!name || !*name || !pal_environ) return NULL;
    size_t n = 0;
    while (name[n]) {
        if (name[n] == '=') return NULL;
        ++n;
    }
    for (char **p = pal_environ; *p; ++p) {
        size_t i = 0;
        while (i < n && (*p)[i] == name[i]) ++i;
        if (i == n && (*p)[i] == '=') return *p + i + 1;
    }
    return NULL;
}

int system(const char *command) {
    int status = 0;
    int rc = pal_system(command, &status);
    if (rc < 0) { libc_pal_fail(rc); return -1; }
    return status;
}

static void swap_bytes(unsigned char *a, unsigned char *b, size_t n) {
    while (n--) { unsigned char t = *a; *a++ = *b; *b++ = t; }
}

/* In-place heapsort: bounded stack and no allocation, including for huge input. */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (!base || !compar || size == 0 || nmemb < 2) return;
    unsigned char *a = (unsigned char *)base;
    size_t start = nmemb / 2;
    size_t end = nmemb;
    while (end > 1) {
        size_t root;
        if (start) root = --start;
        else {
            --end;
            swap_bytes(a, a + end * size, size);
            root = 0;
        }
        for (;;) {
            if (root >= end / 2) break;
            size_t child = root * 2 + 1;
            if (child + 1 < end && compar(a + child * size, a + (child + 1) * size) < 0)
                ++child;
            if (compar(a + root * size, a + child * size) >= 0) break;
            swap_bytes(a + root * size, a + child * size, size);
            root = child;
        }
    }
}

void *(bsearch)(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (!key || !base || !compar || size == 0) return NULL;
    const unsigned char *a = (const unsigned char *)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *item = a + mid * size;
        int c = compar(key, item);
        if (c < 0) hi = mid;
        else if (c > 0) lo = mid + 1;
        else return (void *)item;
    }
    return NULL;
}
