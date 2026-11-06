#include "util.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_err[1024];

const char *na_errmsg(void) { return g_err; }

void na_seterr(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int na_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return -1;
}

void *na_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fputs("nextagent: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

void *na_realloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("nextagent: out of memory\n", stderr);
        exit(2);
    }
    return q;
}

char *na_strndup(const char *s, size_t n) {
    char *r = na_alloc(n + 1);
    if (n) memcpy(r, s, n);
    r[n] = 0;
    return r;
}

char *na_strdup(const char *s) { return na_strndup(s, strlen(s)); }

void buf_init(Buf *b) {
    b->p = NULL;
    b->len = b->cap = 0;
}

void buf_free(Buf *b) {
    free(b->p);
    buf_init(b);
}

static void buf_grow(Buf *b, size_t extra) {
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    b->p = na_realloc(b->p, cap);
    b->cap = cap;
}

void buf_put(Buf *b, const void *data, size_t n) {
    buf_grow(b, n);
    if (n) memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = 0;
}

void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
void buf_putc(Buf *b, char c) { buf_put(b, &c, 1); }

void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) {
        buf_put(b, tmp, (size_t)n);
        return;
    }
    char *big = na_alloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_put(b, big, (size_t)n);
    free(big);
}

char *buf_take(Buf *b) {
    if (!b->p) buf_grow(b, 0);
    b->p[b->len] = 0;
    char *r = b->p;
    buf_init(b);
    return r;
}

void sl_init(StrList *l) {
    l->v = NULL;
    l->n = l->cap = 0;
}

void sl_free(StrList *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    sl_init(l);
}

void sl_push(StrList *l, const char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = na_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = na_strdup(s);
}

bool sl_has(const StrList *l, const char *s) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i], s) == 0) return true;
    return false;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void sl_sort(StrList *l) {
    if (l->n > 1) qsort(l->v, l->n, sizeof *l->v, cmp_str);
}

static uint32_t ci_hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint32_t)tolower((unsigned char)*s)) * 16777619u;
    return h;
}

void ss_init(StrSet *s) {
    s->slots = NULL;
    s->cap = s->n = 0;
}

void ss_free(StrSet *s) {
    for (size_t i = 0; i < s->cap; i++) free(s->slots[i]);
    free(s->slots);
    ss_init(s);
}

bool ss_has(const StrSet *s, const char *str) {
    if (!s->cap) return false;
    size_t i = ci_hash(str) & (s->cap - 1);
    while (s->slots[i]) {
        if (path_equal(s->slots[i], str)) return true;
        i = (i + 1) & (s->cap - 1);
    }
    return false;
}

bool ss_add(StrSet *s, const char *str) {
    if (ss_has(s, str)) return false;
    if ((s->n + 1) * 2 > s->cap) {
        size_t ncap = s->cap ? s->cap * 2 : 16;
        char **old = s->slots;
        size_t ocap = s->cap;
        s->slots = na_alloc(ncap * sizeof *s->slots);
        s->cap = ncap;
        for (size_t k = 0; k < ocap; k++) {
            if (!old[k]) continue;
            size_t i = ci_hash(old[k]) & (ncap - 1);
            while (s->slots[i]) i = (i + 1) & (ncap - 1);
            s->slots[i] = old[k];
        }
        free(old);
    }
    size_t i = ci_hash(str) & (s->cap - 1);
    while (s->slots[i]) i = (i + 1) & (s->cap - 1);
    s->slots[i] = na_strdup(str);
    s->n++;
    return true;
}

size_t utf8_seq_len(unsigned char c) {
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return 2;
    if (c >= 0xE0 && c <= 0xEF) return 3;
    if (c >= 0xF0 && c <= 0xF4) return 4;
    return 0;
}

bool utf8_valid(const char *s, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        size_t l = utf8_seq_len(c);
        if (l == 0 || i + l > n) return false;
        if (l >= 2) {
            for (size_t k = 1; k < l; k++)
                if ((p[i + k] & 0xC0) != 0x80) return false;
            if (c == 0xE0 && p[i + 1] < 0xA0) return false;
            if (c == 0xED && p[i + 1] > 0x9F) return false; /* surrogates */
            if (c == 0xF0 && p[i + 1] < 0x90) return false;
            if (c == 0xF4 && p[i + 1] > 0x8F) return false;
        }
        i += l;
    }
    return true;
}

size_t utf8_cp_count(const char *s, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) c++;
    return c;
}

/* ---- BLAKE2b ---- */
static const uint64_t B2_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};

static const uint8_t B2_SIGMA[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

static uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

static void b2_g(uint64_t *v, int a, int b, int c, int d, uint64_t x, uint64_t y) {
    v[a] = v[a] + v[b] + x;
    v[d] = rotr64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = rotr64(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr64(v[b] ^ v[c], 63);
}

static void b2_compress(uint64_t h[8], const uint8_t block[128], uint64_t t, bool last) {
    uint64_t m[16], v[16];
    for (int i = 0; i < 16; i++) {
        uint64_t w = 0;
        for (int k = 7; k >= 0; k--) w = (w << 8) | block[i * 8 + k];
        m[i] = w;
    }
    for (int i = 0; i < 8; i++) {
        v[i] = h[i];
        v[i + 8] = B2_IV[i];
    }
    v[12] ^= t;
    if (last) v[14] = ~v[14];
    for (int r = 0; r < 12; r++) {
        const uint8_t *s = B2_SIGMA[r];
        b2_g(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        b2_g(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        b2_g(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        b2_g(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        b2_g(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        b2_g(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        b2_g(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        b2_g(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[i + 8];
}

void blake2b16(const void *data, size_t n, uint8_t out[16]) {
    const uint8_t *p = data;
    uint64_t h[8];
    for (int i = 0; i < 8; i++) h[i] = B2_IV[i];
    h[0] ^= 0x01010000ULL ^ 16ULL; /* fanout=1 depth=1 keylen=0 outlen=16 */
    uint64_t t = 0;
    while (n > 128) {
        t += 128;
        b2_compress(h, p, t, false);
        p += 128;
        n -= 128;
    }
    uint8_t block[128];
    memset(block, 0, sizeof block);
    if (n) memcpy(block, p, n);
    t += n;
    b2_compress(h, block, t, true);
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(h[i / 8] >> (8 * (i % 8)));
}

void hex_encode(const uint8_t *in, size_t n, char *out) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hex_decode(const char *in, uint8_t *out, size_t n) {
    if (strlen(in) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        int a = hexval(in[2 * i]), b = hexval(in[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return true;
}

/* ---- paths ---- */
char *path_normalize(const char *path) {
    size_t n = strlen(path);
    char *tmp = na_alloc(n + 1);
    for (size_t i = 0; i < n; i++) tmp[i] = path[i] == '\\' ? '/' : path[i];
    size_t pre = 0;
    bool absolute = false;
    if (n >= 2 && tmp[1] == ':' && isalpha((unsigned char)tmp[0])) {
        pre = 2;
        if (n > 2 && tmp[2] == '/') {
            pre = 3;
            absolute = true;
        }
    } else if (n >= 1 && tmp[0] == '/') {
        pre = 1;
        absolute = true;
    }
    StrList parts;
    sl_init(&parts);
    size_t i = pre;
    while (i <= n) {
        size_t j = i;
        while (j < n && tmp[j] != '/') j++;
        size_t cl = j - i;
        if (cl == 0 || (cl == 1 && tmp[i] == '.')) {
            /* skip */
        } else if (cl == 2 && tmp[i] == '.' && tmp[i + 1] == '.') {
            if (parts.n > 0 && strcmp(parts.v[parts.n - 1], "..") != 0) {
                free(parts.v[--parts.n]);
            } else if (!absolute) {
                sl_push(&parts, "..");
            }
        } else {
            char *c = na_strndup(tmp + i, cl);
            sl_push(&parts, c);
            free(c);
        }
        i = j + 1;
    }
    Buf out;
    buf_init(&out);
    buf_put(&out, tmp, pre);
    for (size_t k = 0; k < parts.n; k++) {
        if (k > 0) buf_putc(&out, '/');
        buf_puts(&out, parts.v[k]);
    }
    if (parts.n == 0 && pre == 0) buf_putc(&out, '.');
    sl_free(&parts);
    free(tmp);
    return buf_take(&out);
}

static bool ci_eq(char a, char b) {
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

bool path_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (!ci_eq(*a, *b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool path_inside(const char *root, const char *path) {
    size_t rl = strlen(root);
    while (rl > 0 && root[rl - 1] == '/') rl--;
    if (strlen(path) <= rl + 1) return false;
    for (size_t i = 0; i < rl; i++)
        if (!ci_eq(root[i], path[i])) return false;
    return path[rl] == '/';
}

bool path_has_suffix_ci(const char *path, const char *suffix) {
    size_t a = strlen(path), b = strlen(suffix);
    if (b > a) return false;
    for (size_t i = 0; i < b; i++)
        if (!ci_eq(path[a - b + i], suffix[i])) return false;
    return true;
}

bool path_inside_or_equal(const char *root, const char *path) {
    size_t rl = strlen(root);
    while (rl > 1 && root[rl - 1] == '/') rl--;
    size_t pl = strlen(path);
    while (pl > 1 && path[pl - 1] == '/') pl--;
    if (pl == rl) {
        for (size_t i = 0; i < rl; i++)
            if (!ci_eq(root[i], path[i])) return false;
        return true;
    }
    return path_inside(root, path);
}
