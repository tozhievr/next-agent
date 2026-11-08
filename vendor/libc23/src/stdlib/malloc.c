/*
 * malloc.c - general purpose allocator on top of pal_mem_map.
 *
 *  - every block is preceded by a 32 byte header {capacity, tag, requested size}
 *  - small requests (<= MAX_SMALL) are served from size classes carved out of
 *    1 MiB regions (4 classes per power of two above 128 bytes); freed blocks
 *    go to a per-class free list
 *  - larger requests are mapped directly (page granular) and unmapped on free
 *  - alignments above 16 are served by over-allocating and placing a fake
 *    header (tag ALIGNED, capacity = offset to the real block) before the
 *    aligned address
 */
#include "libc.h"
#include <stdlib.h>

#define HDR          32u
#define NCLASS       44
#define MAX_SMALL    65536u
#define REGION_SIZE  (1u << 20)
#define TAG_LARGE    ((size_t)-1)
#define TAG_ALIGNED  ((size_t)-2)

struct hdr { size_t cap; size_t tag; size_t requested; size_t reserved; };

static libc_lock_t heap_lock = LIBC_LOCK_INIT;
static void *freelist[NCLASS];
static char *bump_ptr;
static size_t bump_left;

static inline struct hdr *hdr_of(void *p) { return (struct hdr *)((char *)p - HDR); }

static size_t class_size(int c) {
    if (c < 8) return (size_t)(c + 1) * 16;
    int k = 7 + (c - 8) / 4, sub = (c - 8) % 4;
    return (size_t)(5 + sub) << (k - 2);
}

static int class_index(size_t n) {
    if (n <= 128) return n ? (int)((n + 15) / 16) - 1 : 0;
    size_t m = n - 1;
    int k = 63 - __builtin_clzll(m);
    int sub = (int)((m >> (k - 2)) & 3);
    return 8 + (k - 7) * 4 + sub;
}

static size_t round_up(size_t n, size_t a) { return (n + a - 1) / a * a; }

static void *alloc_small(int c, size_t requested) {
    void *p;
    libc_lock(&heap_lock);
    p = freelist[c];
    if (p) {
        freelist[c] = *(void **)p;
    } else {
        size_t total = HDR + class_size(c);
        if (bump_left < total) {
            char *r = pal_mem_map(REGION_SIZE);
            if (!r) { libc_unlock(&heap_lock); return NULL; }
            bump_ptr = r;
            bump_left = REGION_SIZE;
        }
        struct hdr *h = (struct hdr *)bump_ptr;
        bump_ptr += total;
        bump_left -= total;
        h->cap = class_size(c);
        h->tag = (size_t)c;
        h->requested = requested;
        p = (char *)h + HDR;
    }
    struct hdr *h = hdr_of(p);
    h->requested = requested;
    libc_unlock(&heap_lock);
    return p;
}

static void *alloc_large(size_t n) {
    size_t ps = pal_page_size();
    if (n > SIZE_MAX - HDR - ps) return NULL;
    size_t map = round_up(n + HDR, ps);
    struct hdr *h = pal_mem_map(map);
    if (!h) return NULL;
    h->cap = map - HDR;
    h->tag = TAG_LARGE;
    h->requested = n;
    return (char *)h + HDR;
}

/* *fresh set to 1 when the memory is known to be zero (direct mapping) */
static void *alloc_internal(size_t n, int *fresh) {
    if (n > (size_t)PTRDIFF_MAX) { errno = ENOMEM; return NULL; }
    void *p;
    if (n <= MAX_SMALL) {
        p = alloc_small(class_index(n), n);
    } else {
        p = alloc_large(n);
        if (p && fresh) *fresh = 1;
    }
    if (!p) errno = ENOMEM;
    return p;
}

static void free_internal(void *p) {
    struct hdr *h = hdr_of(p);
    if (h->tag == TAG_ALIGNED) {
        p = (char *)p - h->cap;
        h = hdr_of(p);
    }
    if (h->tag == TAG_LARGE) {
        pal_mem_unmap(h, h->cap + HDR);
        return;
    }
    int c = (int)h->tag;
    libc_lock(&heap_lock);
    *(void **)p = freelist[c];
    freelist[c] = p;
    libc_unlock(&heap_lock);
}

/* usable bytes of an allocated block */
static size_t usable(void *p) {
    struct hdr *h = hdr_of(p);
    if (h->tag == TAG_ALIGNED) {
        size_t off = h->cap;
        return hdr_of((char *)p - off)->cap - off;
    }
    return h->cap;
}

static size_t requested_size(void *p) {
    return hdr_of(p)->requested;
}

void *malloc(size_t n) {
    return alloc_internal(n, NULL);
}

void *__libc_malloc_internal(size_t n) { return malloc(n); }

void *calloc(size_t a, size_t b) {
    size_t n;
    if (__builtin_mul_overflow(a, b, &n)) { errno = ENOMEM; return NULL; }
    int fresh = 0;
    void *p = alloc_internal(n, &fresh);
    if (p && !fresh) __builtin_memset(p, 0, n);
    return p;
}

void free(void *p) {
    if (p) free_internal(p);
}

void free_sized(void *p, size_t size) { (void)size; free(p); }
void free_aligned_sized(void *p, size_t alignment, size_t size) { (void)alignment; (void)size; free(p); }

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free_internal(p); return NULL; } /* C23: UB; behave like glibc */
    if (n > (size_t)PTRDIFF_MAX) { errno = ENOMEM; return NULL; }
    size_t cap = usable(p);
    size_t old_size = requested_size(p);
    struct hdr *h = hdr_of(p);
    if (n <= cap) {
        /* keep the block unless it is a big block that would waste > half */
        if (h->tag != TAG_LARGE || n >= cap / 2) {
            h->requested = n;
            return p;
        }
        /* heavily shrinking a mapped block: move to a smaller block */
    }
    void *q = malloc(n);
    if (!q) return NULL;
    __builtin_memcpy(q, p, old_size < n ? old_size : n);
    free_internal(p);
    return q;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1))) { errno = EINVAL; return NULL; }
    if (alignment <= 16) return malloc(size);
    if (alignment > SIZE_MAX - HDR || size > SIZE_MAX - alignment - HDR) {
        errno = ENOMEM;
        return NULL;
    }
    char *raw = malloc(size + alignment + HDR);
    if (!raw) return NULL;
    uintptr_t a = ((uintptr_t)raw + HDR + alignment - 1) & ~(uintptr_t)(alignment - 1);
    char *user = (char *)a;
    struct hdr *fake = hdr_of(user);
    fake->cap = (size_t)(user - raw);
    fake->tag = TAG_ALIGNED;
    fake->requested = size;
    return user;
}

