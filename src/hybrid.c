/* Hybrid code search over a persistent in-memory index.
 *
 * Signals, fused with weighted reciprocal rank fusion:
 *   1. BM25 over the words of each chunk (identifiers are split into camelCase / snake_case words
 *      and light-stemmed, the whole identifier is kept too),
 *   2. BM25 over the names (symbol, scope, path) of each chunk,
 *   3. kNN over local embeddings: "random indexing" learns, from the workspace itself, which words
 *      occur in the same code (a word's vector is the sum of the sparse random codes of everything it
 *      co-occurs with), so `flush` ends up near `write`, `disk`, `sync` of THIS code base. Chunk and
 *      query vectors are weighted sums of word vectors; similarity is the cosine.
 * A chunk is a symbol (or a 40-line window of a long stretch). Per-file chunks are cached by content
 * hash, so a query after a small edit re-tokenizes only the changed files. All arithmetic is plain
 * IEEE with no libm and a fixed evaluation order, so results are identical on every platform. */
#include "semantic.h"
#include "embed.h"
#include "platform.h"
#include "bin.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW 40
#define MAX_FILE_BYTES (2u << 20)
#define DIM 128
#define MIN_DL 48

/* ---- string dictionary: word -> dense id ---- */
typedef struct {
    char **k;
    uint32_t *slots; /* id + 1 */
    size_t n, cap, scap;
} Dict;

static uint64_t fnv(const char *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
    return h;
}

static void dict_rehash(Dict *d) {
    size_t cap = d->scap ? d->scap * 2 : 4096;
    free(d->slots);
    d->slots = na_alloc(cap * sizeof *d->slots);
    memset(d->slots, 0, cap * sizeof *d->slots);
    d->scap = cap;
    for (size_t i = 0; i < d->n; i++) {
        size_t h = fnv(d->k[i], strlen(d->k[i])) & (cap - 1);
        while (d->slots[h]) h = (h + 1) & (cap - 1);
        d->slots[h] = (uint32_t)i + 1;
    }
}

/* id of the word, or (uint32_t)-1 when absent and !add */
static uint32_t dict_id(Dict *d, const char *s, size_t n, bool add) {
    if (d->scap == 0) {
        if (!add) return (uint32_t)-1;
        dict_rehash(d);
    }
    size_t h = fnv(s, n) & (d->scap - 1);
    while (d->slots[h]) {
        const char *k = d->k[d->slots[h] - 1];
        if (strlen(k) == n && memcmp(k, s, n) == 0) return d->slots[h] - 1;
        h = (h + 1) & (d->scap - 1);
    }
    if (!add) return (uint32_t)-1;
    if (d->n == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 1024;
        d->k = na_realloc(d->k, d->cap * sizeof *d->k);
    }
    char *c = na_alloc(n + 1);
    memcpy(c, s, n);
    c[n] = 0;
    d->k[d->n] = c;
    d->slots[h] = (uint32_t)++d->n;
    if (d->n * 2 > d->scap) dict_rehash(d);
    return (uint32_t)d->n - 1;
}

/* ---- cache ---- */
typedef struct {
    uint32_t first, last; /* 1-based inclusive lines */
    char *sym, *kind;     /* label, may be NULL */
    uint32_t *bt, *nt;    /* sorted (term id, tf) pairs for the body / the names */
    uint32_t nbt, nnt;    /* pair counts */
    uint32_t blen, nlen;  /* token totals */
    float *vec;           /* DIM floats, NULL when not (yet) embeddable */
    uint64_t ekey;        /* key of the external embedding of this chunk (0: no embedder) */
    char *etext;          /* text for the external model, kept until it has been embedded */
    uint32_t elen;
} HChunk;

typedef struct {
    char *path;
    uint64_t hash;
    HChunk *c;
    size_t n;
} HFile;

typedef struct {
    Dict terms;
    HFile *f;
    size_t nf;
    uint64_t version, built; /* corpus changes / version the embeddings were built for */
    float *W;                /* word vectors, one row per kept term */
    int32_t *row;            /* term id -> row or -1 */
    size_t nrow, rowcap;
    uint32_t *df;
    size_t dfcap;
    Embedder *emb; /* external model from .aed/embed.json, or NULL */
    bool emb_tried;
    bool loaded;        /* the section of .aed/index.cache was offered to this cache */
    uint64_t saved_ver; /* corpus version that is on disk */
} Hyb;

static void hfile_free(HFile *f) {
    for (size_t i = 0; i < f->n; i++) {
        free(f->c[i].sym);
        free(f->c[i].kind);
        free(f->c[i].bt);
        free(f->c[i].nt);
        free(f->c[i].vec);
        free(f->c[i].etext);
    }
    free(f->c);
    free(f->path);
}

static void hyb_free(void *p) {
    Hyb *h = p;
    for (size_t i = 0; i < h->nf; i++) hfile_free(&h->f[i]);
    free(h->f);
    for (size_t i = 0; i < h->terms.n; i++) free(h->terms.k[i]);
    free(h->terms.k);
    free(h->terms.slots);
    free(h->W);
    free(h->row);
    free(h->df);
    embed_close(h->emb);
    free(h);
}

/* ---- tokens ---- */
typedef struct {
    uint32_t *v;
    size_t n, cap;
} U32;

static void u32_push(U32 *u, uint32_t x) {
    if (u->n == u->cap) {
        u->cap = u->cap ? u->cap * 2 : 256;
        u->v = na_realloc(u->v, u->cap * sizeof *u->v);
    }
    u->v[u->n++] = x;
}

typedef void (*Emit)(void *ctx, const char *w, size_t n);

static bool idc(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

/* length after stripping a plural / participle suffix (words of 5+ letters only) */
static size_t stem_len(const char *w, size_t n) {
    if (n < 5) return n;
    if (n >= 6 && memcmp(w + n - 3, "ing", 3) == 0) return n - 3;
    if (n >= 6 && memcmp(w + n - 3, "ies", 3) == 0) return n - 3;
    if (memcmp(w + n - 2, "ed", 2) == 0 || memcmp(w + n - 2, "es", 2) == 0) return n - 2;
    if (w[n - 1] == 's' && w[n - 2] != 's' && w[n - 2] != 'u') return n - 1;
    return n;
}

static void emit_word(const char *w, size_t n, Emit emit, void *ctx) {
    emit(ctx, w, n);
    size_t m = stem_len(w, n);
    if (m != n && m >= 3) emit(ctx, w, m);
}

static void ident_words(const char *s, size_t n, Emit emit, void *ctx) {
    char low[96];
    if (n < 2) return;
    if (n >= sizeof low) n = sizeof low - 1;
    size_t a = 0;
    for (size_t i = 0; i < n; i++) low[i] = (char)tolower((unsigned char)s[i]);
    for (size_t i = 1; i <= n; i++) {
        bool cut = i == n || s[i] == '_' || s[i - 1] == '_';
        if (!cut) {
            unsigned char p = (unsigned char)s[i - 1], c = (unsigned char)s[i];
            bool dp = isdigit(p), dc = isdigit(c);
            cut = dp != dc || (islower(p) && isupper(c)) ||
                  (isupper(p) && isupper(c) && i + 1 < n && islower((unsigned char)s[i + 1]));
        }
        if (cut) {
            size_t b = i;
            while (a < b && s[a] == '_') a++;
            if (b - a >= 2 && b - a != n) emit_word(low + a, b - a, emit, ctx);
            a = i;
        }
    }
    emit_word(low, n, emit, ctx); /* the whole identifier, so exact names rank first */
}

static void tokenize(const char *t, size_t n, Emit emit, void *ctx) {
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)t[i];
        if (idc(c) && !isdigit(c)) {
            size_t s = i;
            while (i < n && idc((unsigned char)t[i])) i++;
            ident_words(t + s, i - s, emit, ctx);
        } else if (isdigit(c)) {
            while (i < n && idc((unsigned char)t[i])) i++;
        } else {
            i++;
        }
    }
}

typedef struct {
    Dict *d;
    U32 ids;
} Collect;

static void collect_emit(void *c, const char *w, size_t n) {
    Collect *k = c;
    u32_push(&k->ids, dict_id(k->d, w, n, true));
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* ids -> sorted (id, tf) pairs; returns the pair count, *total = number of tokens */
static uint32_t compress(U32 *u, uint32_t **out, uint32_t *total) {
    qsort(u->v, u->n, sizeof *u->v, cmp_u32);
    *total = (uint32_t)u->n;
    uint32_t *p = na_alloc((u->n ? u->n * 2 : 2) * sizeof *p);
    uint32_t np = 0;
    for (size_t i = 0; i < u->n;) {
        size_t j = i;
        while (j < u->n && u->v[j] == u->v[i]) j++;
        p[np * 2] = u->v[i];
        p[np * 2 + 1] = (uint32_t)(j - i);
        np++;
        i = j;
    }
    *out = p;
    u->n = 0;
    return np;
}

static uint32_t pair_tf(const uint32_t *p, uint32_t np, uint32_t id) {
    size_t lo = 0, hi = np;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (p[m * 2] < id) lo = m + 1;
        else hi = m;
    }
    return lo < np && p[lo * 2] == id ? p[lo * 2 + 1] : 0;
}

/* ---- math without libm ---- */
static double dlog(double x) {
    const double ln2 = 0.6931471805599453;
    int e = 0;
    while (x > 1.5) x /= 2, e++;
    while (x < 0.75) x *= 2, e--;
    double y = (x - 1) / (x + 1), y2 = y * y, term = y, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= y2;
    }
    return 2 * sum + e * ln2;
}

static double dsqrt(double x) {
    if (x <= 0) return 0;
    double g = (x + 1) / 2;
    for (int i = 0; i < 60; i++) g = 0.5 * (g + x / g);
    return g;
}

/* ---- building the per-file cache ---- */
static bool boundary_symbol(const Symbol *y) { return y->line >= 1; }

static void build_file(Hyb *h, Sem *s, HFile *hf, size_t fi) {
    hf->c = NULL;
    hf->n = 0;
    const char *rel = hf->path;
    char *text = NULL;
    size_t len;
    if (store_snapshot_text(s->st, rel, &text, &len) != 0 || len > MAX_FILE_BYTES || !utf8_valid(text, len)) {
        free(text);
        return;
    }
    size_t nl = 1;
    for (size_t i = 0; i + 1 < len; i++)
        if (text[i] == '\n') nl++;
    size_t *ls = na_alloc((nl + 2) * sizeof *ls);
    ls[0] = 0;
    size_t k = 1;
    for (size_t i = 0; i + 1 < len; i++)
        if (text[i] == '\n') ls[k++] = i + 1;
    ls[nl] = len;
    const Symbol **label = na_alloc((nl + 2) * sizeof *label);
    memset(label, 0, (nl + 2) * sizeof *label);
    const SymbolList *sl = wsi_symbols(s->ix, fi);
    for (size_t i = 0; i < sl->n; i++)
        if (boundary_symbol(&sl->v[i]) && sl->v[i].line <= nl) label[sl->v[i].line] = &sl->v[i];

    /* a doc comment directly above a symbol belongs to that symbol */
    for (size_t L = 2; L <= nl; L++) {
        if (!label[L]) continue;
        size_t top = L;
        while (top > 1 && !label[top - 1]) {
            const char *p = text + ls[top - 2], *end = text + ls[top - 1];
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            bool cm = (end - p >= 2 && ((p[0] == '/' && (p[1] == '/' || p[1] == '*')) || (p[0] == '*' && (p[1] == ' ' || p[1] == '/' || p[1] == '\r' || p[1] == '\n'))));
            if (!cm) break;
            top--;
        }
        if (top < L) {
            label[top] = label[L];
            label[L] = NULL;
        }
    }
    Collect col = {&h->terms, {0}};
    size_t cap = 0;
    const Symbol *cur = NULL;
    size_t first = 1;
    for (size_t line = 2; line <= nl + 1; line++) {
        if (!(line == nl + 1 || label[line] || line - first >= WINDOW)) continue;
        if (label[first]) cur = label[first];
        if (hf->n == cap) {
            cap = cap ? cap * 2 : 8;
            hf->c = na_realloc(hf->c, cap * sizeof *hf->c);
        }
        HChunk *c = &hf->c[hf->n++];
        memset(c, 0, sizeof *c);
        c->first = (uint32_t)first;
        c->last = (uint32_t)(line - 1);
        if (cur) {
            c->sym = na_strdup(cur->name);
            c->kind = na_strdup(cur->kind);
        }
        size_t a = ls[first - 1], e = ls[line - 1];
        tokenize(text + a, e - a, collect_emit, &col);
        c->nbt = compress(&col.ids, &c->bt, &c->blen);
        if (cur) {
            tokenize(cur->name, strlen(cur->name), collect_emit, &col);
            if (cur->scope) tokenize(cur->scope, strlen(cur->scope), collect_emit, &col);
        }
        tokenize(rel, strlen(rel), collect_emit, &col);
        c->nnt = compress(&col.ids, &c->nt, &c->nlen);
        if (h->emb) { /* what the external model will see: the label, then the chunk, cut at max_chars */
            Buf et;
            buf_init(&et);
            if (cur) buf_printf(&et, "%s %s\n", cur->kind, cur->name);
            size_t room = embed_max_chars(h->emb), take = e - a < room ? e - a : room;
            while (take > 0 && take < e - a && ((unsigned char)text[a + take] & 0xC0) == 0x80) take--;
            buf_put(&et, text + a, take);
            c->ekey = embed_key(h->emb, et.p ? et.p : "", et.len);
            if (embed_get(h->emb, c->ekey)) buf_free(&et);
            else {
                c->elen = (uint32_t)et.len;
                c->etext = buf_take(&et);
            }
        }
        first = line;
    }
    free(col.ids.v);
    free(label);
    free(ls);
    free(text);
    (void)h;
}

static void refresh_cache(Hyb *h, Sem *s) {
    size_t n = wsi_nfiles(s->ix), j = 0;
    HFile *nv = na_alloc((n ? n : 1) * sizeof *nv);
    for (size_t i = 0; i < n; i++) {
        const char *p = wsi_path(s->ix, i);
        while (j < h->nf && strcmp(h->f[j].path, p) < 0) {
            hfile_free(&h->f[j++]);
            h->version++;
        }
        if (j < h->nf && strcmp(h->f[j].path, p) == 0) {
            if (h->f[j].hash == wsi_hash(s->ix, i)) {
                nv[i] = h->f[j++];
                continue;
            }
            hfile_free(&h->f[j++]);
        }
        memset(&nv[i], 0, sizeof nv[i]);
        nv[i].path = na_strdup(p);
        nv[i].hash = wsi_hash(s->ix, i);
        build_file(h, s, &nv[i], i);
        h->version++;
    }
    while (j < h->nf) {
        hfile_free(&h->f[j++]);
        h->version++;
    }
    free(h->f);
    h->f = nv;
    h->nf = n;
}

/* ---- local embeddings (random indexing) ---- */
static void add_code(double *dst, const char *term, double w) {
    uint64_t x = fnv(term, strlen(term)) | 1;
    for (int k = 0; k < 8; k++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        dst[(x >> 8) % DIM] += (x & 1) ? w : -w;
    }
}

static void unit(double *v) {
    double s = 0;
    for (int i = 0; i < DIM; i++) s += v[i] * v[i];
    s = dsqrt(s);
    if (s > 1e-9)
        for (int i = 0; i < DIM; i++) v[i] /= s;
}

static double idf_of(double n, double df) { return dlog(1 + n / df); }

static void build_embeddings(Hyb *h) {
    size_t N = 0;
    for (size_t f = 0; f < h->nf; f++) N += h->f[f].n;
    size_t nt = h->terms.n;
    if (h->dfcap < nt) {
        h->dfcap = nt;
        h->df = na_realloc(h->df, (nt ? nt : 1) * sizeof *h->df);
    }
    memset(h->df, 0, nt * sizeof *h->df);
    for (size_t f = 0; f < h->nf; f++)
        for (size_t i = 0; i < h->f[f].n; i++)
            for (uint32_t k = 0; k < h->f[f].c[i].nbt; k++) h->df[h->f[f].c[i].bt[k * 2]]++;
    free(h->row);
    h->row = na_alloc((nt ? nt : 1) * sizeof *h->row);
    size_t rows = 0;
    for (size_t t = 0; t < nt; t++) {
        bool keep = h->df[t] >= 2 && (N < 8 || (uint64_t)h->df[t] * 2 <= N);
        h->row[t] = keep ? (int32_t)rows++ : -1;
    }
    free(h->W);
    h->W = na_alloc((rows ? rows * DIM : 1) * sizeof *h->W);
    double *C = na_alloc((rows ? rows * DIM : 1) * sizeof *C);
    memset(C, 0, (rows ? rows * DIM : 1) * sizeof *C);
    h->nrow = rows;

    /* pass 1: every kept word accumulates the (unit) code sum of each chunk it occurs in */
    double raw[DIM];
    for (size_t f = 0; f < h->nf; f++)
        for (size_t i = 0; i < h->f[f].n; i++) {
            const HChunk *c = &h->f[f].c[i];
            memset(raw, 0, sizeof raw);
            for (uint32_t k = 0; k < c->nbt; k++) {
                uint32_t t = c->bt[k * 2];
                if (h->row[t] < 0) continue;
                add_code(raw, h->terms.k[t], idf_of((double)N, h->df[t]) * dsqrt((double)c->bt[k * 2 + 1]));
            }
            unit(raw);
            for (uint32_t k = 0; k < c->nbt; k++) {
                int32_t r = h->row[c->bt[k * 2]];
                if (r < 0) continue;
                double *dst = C + (size_t)r * DIM;
                for (int d = 0; d < DIM; d++) dst[d] += raw[d];
            }
        }
    /* remove the common direction, then normalise */
    double mean[DIM];
    memset(mean, 0, sizeof mean);
    for (size_t r = 0; r < rows; r++) {
        double *v = C + r * DIM;
        unit(v);
        for (int d = 0; d < DIM; d++) mean[d] += v[d];
    }
    for (int d = 0; d < DIM; d++) mean[d] = rows ? mean[d] / (double)rows : 0;
    for (size_t r = 0; r < rows; r++) {
        double *v = C + r * DIM;
        for (int d = 0; d < DIM; d++) v[d] -= mean[d];
        unit(v);
        for (int d = 0; d < DIM; d++) h->W[r * DIM + d] = (float)v[d];
    }
    free(C);

    /* pass 2: chunk vectors = weighted sums of word vectors (names count double) */
    for (size_t f = 0; f < h->nf; f++)
        for (size_t i = 0; i < h->f[f].n; i++) {
            HChunk *c = &h->f[f].c[i];
            free(c->vec);
            c->vec = NULL;
            double v[DIM];
            memset(v, 0, sizeof v);
            bool any = false;
            for (int part = 0; part < 2; part++) {
                const uint32_t *p = part ? c->nt : c->bt;
                uint32_t np = part ? c->nnt : c->nbt;
                for (uint32_t k = 0; k < np; k++) {
                    uint32_t t = p[k * 2];
                    int32_t r = h->row[t];
                    if (r < 0) continue;
                    double w = idf_of((double)N, h->df[t]) * dsqrt((double)p[k * 2 + 1]) * (part ? 2.0 : 1.0);
                    const float *wv = h->W + (size_t)r * DIM;
                    for (int d = 0; d < DIM; d++) v[d] += w * wv[d];
                    any = true;
                }
            }
            if (!any) continue;
            unit(v);
            c->vec = na_alloc(DIM * sizeof *c->vec);
            for (int d = 0; d < DIM; d++) c->vec[d] = (float)v[d];
        }
    h->built = h->version;
}

/* ---- query ---- */
typedef struct {
    char **w;
    size_t n, cap;
} Words;

static void words_emit(void *c, const char *w, size_t n) {
    Words *l = c;
    for (size_t i = 0; i < l->n; i++)
        if (strlen(l->w[i]) == n && memcmp(l->w[i], w, n) == 0) return;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->w = na_realloc(l->w, l->cap * sizeof *l->w);
    }
    char *d = na_alloc(n + 1);
    memcpy(d, w, n);
    d[n] = 0;
    l->w[l->n++] = d;
}

static void words_free(Words *l) {
    for (size_t i = 0; i < l->n; i++) free(l->w[i]);
    free(l->w);
}

static bool stop_word(const char *w) {
    static const char *const s[] = {"the", "of", "to", "in", "for", "is", "and", "how", "does", "where", "what", "with", "an", "or", "do", "it", "on", "by", "we", "are", "be", "this", "that", "from"};
    for (size_t i = 0; i < sizeof s / sizeof *s; i++)
        if (strcmp(w, s[i]) == 0) return true;
    return false;
}

typedef struct {
    double s;
    uint32_t id;
} Rank;

static int cmp_rank(const void *a, const void *b) {
    const Rank *x = a, *y = b;
    if (x->s != y->s) return x->s > y->s ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

static void ranks_of(const double *score, size_t n, double min, double *rank_out) {
    Rank *r = na_alloc((n ? n : 1) * sizeof *r);
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        rank_out[i] = 0;
        if (score[i] > min) r[m++] = (Rank){score[i], (uint32_t)i};
    }
    qsort(r, m, sizeof *r, cmp_rank);
    for (size_t i = 0; i < m; i++) rank_out[r[i].id] = (double)i + 1; /* 0 = unranked */
    free(r);
}

static bool contains_ci(const char *hay, size_t hn, const char *needle, size_t nn) {
    if (nn == 0 || nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t k = 0;
        while (k < nn && tolower((unsigned char)hay[i + k]) == (unsigned char)needle[k]) k++;
        if (k == nn) return true;
    }
    return false;
}

static double kind_factor(const HChunk *c) {
    double small = c->last - c->first < 2 ? 0.75 : 1.0; /* one-liners (getters, prototypes) match too easily */
    if (!c->sym) return 0.7 * small;
    static const char *const good[] = {"function", "method", "class", "struct", "union", "enum", "typedef", "interface", "trait", "module", "type", "constructor", "namespace"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++)
        if (strcmp(c->kind, good[i]) == 0) return small;
    return 0.8 * small;
}

typedef struct {
    uint32_t file, idx;
} Ref;

/* ---- on-disk section of .aed/index.cache ---- */
static void hyb_save(const Hyb *h, Buf *b) {
    bin_str(b, h->emb ? embed_model(h->emb) : "");
    bin_u32(b, DIM);
    bin_u64(b, h->version);
    bin_u32(b, (uint32_t)h->terms.n);
    for (size_t i = 0; i < h->terms.n; i++) bin_str(b, h->terms.k[i]);
    bin_u32(b, (uint32_t)h->nf);
    for (size_t f = 0; f < h->nf; f++) {
        const HFile *hf = &h->f[f];
        bin_str(b, hf->path);
        bin_u64(b, hf->hash);
        bin_u32(b, (uint32_t)hf->n);
        for (size_t i = 0; i < hf->n; i++) {
            const HChunk *c = &hf->c[i];
            bin_u32(b, c->first);
            bin_u32(b, c->last);
            bin_str(b, c->sym);
            bin_str(b, c->kind);
            bin_u32(b, c->nbt);
            bin_u32(b, c->nnt);
            bin_u32(b, c->blen);
            bin_u32(b, c->nlen);
            for (uint32_t k = 0; k < c->nbt * 2; k++) bin_u32(b, c->bt[k]);
            for (uint32_t k = 0; k < c->nnt * 2; k++) bin_u32(b, c->nt[k]);
            bin_u64(b, c->ekey);
            bin_str(b, c->etext);
            bin_u32(b, c->vec ? 1 : 0);
            if (c->vec) bin_f32s(b, c->vec, DIM);
        }
    }
    bool emb = h->built == h->version && h->row;
    bin_u32(b, emb ? 1 : 0);
    if (emb) {
        bin_u32(b, (uint32_t)h->nrow);
        for (size_t t = 0; t < h->terms.n; t++) bin_u32(b, (uint32_t)h->row[t]);
        for (size_t t = 0; t < h->terms.n; t++) bin_u32(b, h->df[t]);
        bin_f32s(b, h->W, h->nrow * DIM);
    }
}

static void hyb_clear(Hyb *h) {
    for (size_t i = 0; i < h->nf; i++) hfile_free(&h->f[i]);
    free(h->f);
    for (size_t i = 0; i < h->terms.n; i++) free(h->terms.k[i]);
    free(h->terms.k);
    free(h->terms.slots);
    free(h->W);
    free(h->row);
    free(h->df);
    Embedder *emb = h->emb;
    bool tried = h->emb_tried;
    memset(h, 0, sizeof *h);
    h->emb = emb;
    h->emb_tried = tried;
    h->loaded = true;
}

/* Restores a saved section; anything that does not fit (other model, other dimension, damage) is dropped. */
static void hyb_load(Hyb *h, const char *blob, size_t n) {
    BinR r = {(const unsigned char *)blob, n, 0, false};
    char *model = binr_str(&r);
    const char *mine = h->emb ? embed_model(h->emb) : "";
    if (!model || strcmp(model, mine) != 0 || binr_u32(&r) != DIM) r.bad = true;
    free(model);
    uint64_t version = binr_u64(&r);
    uint32_t nt = r.bad ? 0 : binr_u32(&r);
    if (binr_room(&r, nt, 4))
        for (uint32_t i = 0; i < nt && !r.bad; i++) {
            char *s = binr_str(&r);
            if (!s || dict_id(&h->terms, s, strlen(s), true) != i) r.bad = true;
            free(s);
        }
    uint32_t nf = r.bad ? 0 : binr_u32(&r);
    if (binr_room(&r, nf, 8)) {
        h->f = na_alloc((nf ? nf : 1) * sizeof *h->f);
        for (uint32_t f = 0; f < nf && !r.bad; f++) {
            HFile *hf = &h->f[h->nf++];
            hf->path = binr_str(&r);
            hf->hash = binr_u64(&r);
            uint32_t nc = binr_u32(&r);
            if (!binr_room(&r, nc, 16)) break;
            hf->c = na_alloc((nc ? nc : 1) * sizeof *hf->c);
            for (uint32_t i = 0; i < nc && !r.bad; i++) {
                HChunk *c = &hf->c[hf->n++];
                c->first = binr_u32(&r);
                c->last = binr_u32(&r);
                c->sym = binr_str(&r);
                c->kind = binr_str(&r);
                c->nbt = binr_u32(&r);
                c->nnt = binr_u32(&r);
                c->blen = binr_u32(&r);
                c->nlen = binr_u32(&r);
                if (binr_room(&r, (size_t)c->nbt * 2, 4)) {
                    c->bt = na_alloc((c->nbt ? c->nbt * 2 : 1) * sizeof *c->bt);
                    for (uint32_t k = 0; k < c->nbt * 2; k++) c->bt[k] = binr_u32(&r);
                    for (uint32_t k = 0; k < c->nbt; k++)
                        if (c->bt[k * 2] >= nt) r.bad = true;
                }
                if (binr_room(&r, (size_t)c->nnt * 2, 4)) {
                    c->nt = na_alloc((c->nnt ? c->nnt * 2 : 1) * sizeof *c->nt);
                    for (uint32_t k = 0; k < c->nnt * 2; k++) c->nt[k] = binr_u32(&r);
                    for (uint32_t k = 0; k < c->nnt; k++)
                        if (c->nt[k * 2] >= nt) r.bad = true;
                }
                c->ekey = binr_u64(&r);
                c->etext = binr_str(&r);
                if (c->etext) c->elen = (uint32_t)strlen(c->etext);
                if (binr_u32(&r)) {
                    c->vec = na_alloc(DIM * sizeof *c->vec);
                    binr_f32s(&r, c->vec, DIM);
                }
            }
        }
    }
    bool emb = !r.bad && binr_u32(&r) != 0;
    if (emb) {
        uint32_t nrow = binr_u32(&r);
        if (binr_room(&r, nt, 8) && binr_room(&r, (size_t)nrow * DIM, 4)) {
            h->row = na_alloc((nt ? nt : 1) * sizeof *h->row);
            for (uint32_t t = 0; t < nt; t++) {
                h->row[t] = (int32_t)binr_u32(&r);
                if (h->row[t] >= (int32_t)nrow || h->row[t] < -1) r.bad = true;
            }
            h->df = na_alloc((nt ? nt : 1) * sizeof *h->df);
            h->dfcap = nt;
            for (uint32_t t = 0; t < nt; t++) h->df[t] = binr_u32(&r);
            h->W = na_alloc((nrow ? (size_t)nrow * DIM : 1) * sizeof *h->W);
            binr_f32s(&r, h->W, (size_t)nrow * DIM);
            h->nrow = nrow;
        }
    }
    if (r.bad) {
        hyb_clear(h);
        return;
    }
    h->version = version;
    h->built = emb ? version : 0;
    h->saved_ver = version;
}

/* Persists the index and the search cache when something changed since they were loaded. */
void sem_cache_save(WsIndex *ix) {
    if (!ix) return;
    Hyb *h = wsi_user(ix);
    if (!h) {
        if (wsi_dirty(ix)) wsi_cache_save(ix, NULL);
        return;
    }
    if (!wsi_dirty(ix) && h->version == h->saved_ver) return;
    Buf b;
    buf_init(&b);
    hyb_save(h, &b);
    wsi_cache_save(ix, &b);
    h->saved_ver = h->version;
    buf_free(&b);
}

int sem_hybrid(Sem *s, const char *query, const char *path_filter, size_t limit, size_t embed_budget, J **out) {
    size_t qn = strlen(query);
    if (qn == 0) return na_fail("invalid parameter: query must not be empty");
    if (limit == 0 || limit > 100) limit = 10;
    int64_t t0 = pf_unix_ms();
    if (wsi_refresh(s->ix) != 0) return -1;
    int64_t t1 = pf_unix_ms();
    Hyb *h = wsi_user(s->ix);
    if (!h) {
        h = na_alloc(sizeof *h);
        memset(h, 0, sizeof *h);
        wsi_set_user(s->ix, h, hyb_free);
    }
    char *cfg_err = NULL;
    if (!h->emb_tried) {
        h->emb_tried = true;
        h->emb = embed_open(store_root(s->st), &cfg_err);
    }
    if (!h->loaded) {
        h->loaded = true;
        size_t bn;
        char *bp = wsi_take_blob(s->ix, &bn);
        if (bp) {
            hyb_load(h, bp, bn);
            free(bp);
        }
    }
    refresh_cache(h, s);
    int64_t t2 = pf_unix_ms();
    if (h->built != h->version || !h->row) build_embeddings(h);
    if (getenv("NEXTAGENT_TRACE"))
        fprintf(stderr, "trace: index %lld ms (%zu files, %zu read, %zu stat-skipped), chunks %lld ms, embeddings %lld ms\n",
                (long long)(t1 - t0), wsi_nfiles(s->ix), wsi_extracted(s->ix), wsi_skipped(s->ix), (long long)(t2 - t1),
                (long long)(pf_unix_ms() - t2));

    /* flat view of the chunks that pass the path filter */
    size_t total = 0;
    for (size_t f = 0; f < h->nf; f++) total += h->f[f].n;
    Ref *ref = na_alloc((total ? total : 1) * sizeof *ref);
    size_t nch = 0;
    for (size_t f = 0; f < h->nf; f++) {
        if (path_filter && *path_filter && !strstr(h->f[f].path, path_filter)) continue;
        for (size_t i = 0; i < h->f[f].n; i++) ref[nch++] = (Ref){(uint32_t)f, (uint32_t)i};
    }
#define CH(i) (&h->f[ref[i].file].c[ref[i].idx])

    Words q = {0};
    {
        Words raw = {0};
        tokenize(query, qn, words_emit, &raw);
        for (size_t i = 0; i < raw.n; i++)
            if (!stop_word(raw.w[i])) words_emit(&q, raw.w[i], strlen(raw.w[i]));
        words_free(&raw);
    }
    char *lq = na_alloc(qn + 1);
    for (size_t i = 0; i < qn; i++) lq[i] = (char)tolower((unsigned char)query[i]);
    lq[qn] = 0;
    uint32_t *qid = na_alloc((q.n + 1) * sizeof *qid);
    size_t nq = 0;
    for (size_t i = 0; i < q.n; i++) {
        uint32_t id = dict_id(&h->terms, q.w[i], strlen(q.w[i]), false);
        if (id != (uint32_t)-1) qid[nq++] = id;
    }

    /* BM25 on both fields */
    double *score[2];
    for (int fld = 0; fld < 2; fld++) {
        score[fld] = na_alloc((nch + 1) * sizeof(double));
        for (size_t i = 0; i < nch; i++) score[fld][i] = 0;
        double avg = 0;
        uint32_t *dfq = na_alloc((nq + 1) * sizeof *dfq);
        memset(dfq, 0, (nq + 1) * sizeof *dfq);
        for (size_t i = 0; i < nch; i++) {
            const HChunk *c = CH(i);
            avg += fld ? c->nlen : (c->blen < MIN_DL ? MIN_DL : c->blen);
            for (size_t k = 0; k < nq; k++)
                if (pair_tf(fld ? c->nt : c->bt, fld ? c->nnt : c->nbt, qid[k])) dfq[k]++;
        }
        avg = nch ? avg / (double)nch : 1;
        if (avg < 1) avg = 1;
        const double k1 = 1.2, b = fld ? 0.3 : 0.7;
        for (size_t i = 0; i < nch; i++) {
            const HChunk *c = CH(i);
            double len = fld ? c->nlen : (c->blen < MIN_DL ? MIN_DL : c->blen);
            for (size_t k = 0; k < nq; k++) {
                uint32_t tfi = pair_tf(fld ? c->nt : c->bt, fld ? c->nnt : c->nbt, qid[k]);
                if (!tfi) continue;
                double tf = tfi, idf = dlog(1 + ((double)nch - dfq[k] + 0.5) / (dfq[k] + 0.5));
                score[fld][i] += idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * len / avg));
            }
        }
        free(dfq);
    }

    /* kNN: cosine between the query vector and each chunk vector */
    double *sem = na_alloc((nch + 1) * sizeof(double));
    for (size_t i = 0; i < nch; i++) sem[i] = 0;
    {
        double qv[DIM];
        memset(qv, 0, sizeof qv);
        bool any = false;
        for (size_t k = 0; k < nq; k++) {
            int32_t r = h->row[qid[k]];
            if (r < 0) continue;
            double w = idf_of((double)total, h->df[qid[k]]);
            for (int d = 0; d < DIM; d++) qv[d] += w * h->W[(size_t)r * DIM + d];
            any = true;
        }
        if (any) {
            unit(qv);
            for (size_t i = 0; i < nch; i++) {
                const HChunk *c = CH(i);
                if (!c->vec) continue;
                double dot = 0;
                for (int d = 0; d < DIM; d++) dot += qv[d] * c->vec[d];
                sem[i] = dot;
            }
        }
    }

    double *ext = na_alloc((nch + 1) * sizeof *ext);
    for (size_t i = 0; i < nch; i++) ext[i] = 0;
    double *rb = na_alloc((nch + 1) * sizeof *rb), *rn = na_alloc((nch + 1) * sizeof *rn), *re = na_alloc((nch + 1) * sizeof *re),
           *rx = na_alloc((nch + 1) * sizeof *rx);
    Rank *fin = na_alloc((nch + 1) * sizeof *fin);
    size_t nfin = 0, embedded_now = 0;
    char *emb_err = NULL;
    bool use_ext = false;
    for (int pass = 0; pass < 2; pass++) {
        ranks_of(score[0], nch, 0, rb);
        ranks_of(score[1], nch, 0, rn);
        ranks_of(sem, nch, 0.15, re);
        ranks_of(ext, nch, 0, rx);
        nfin = 0;
        for (size_t i = 0; i < nch; i++) {
            double v = 0;
            if (rb[i] > 0) v += 1.0 / (60.0 + rb[i]);
            if (rn[i] > 0) v += 1.5 / (60.0 + rn[i]);
            if (re[i] > 0) v += (use_ext ? 0.4 : 0.8) / (60.0 + re[i]);
            if (rx[i] > 0) v += 1.6 / (60.0 + rx[i]);
            if (v <= 0) continue;
            const HChunk *c = CH(i);
            v *= kind_factor(c);
            if (c->sym && strcmp(c->sym, query) == 0) v += 0.05; /* the query IS the symbol name */
            fin[nfin++] = (Rank){v, (uint32_t)i};
        }
        qsort(fin, nfin, sizeof *fin, cmp_rank);
        if (pass == 1 || !h->emb) break;

        /* external model: embed the query and, within the budget, the best chunks that have no vector yet */
        size_t cap = embed_budget + 1;
        const char **tx = na_alloc(cap * sizeof *tx);
        size_t *tl = na_alloc(cap * sizeof *tl);
        HChunk **todo = na_alloc(cap * sizeof *todo);
        size_t k = 0;
        tx[0] = query;
        tl[0] = qn;
        bool *taken = na_alloc((nch + 1) * sizeof *taken);
        for (size_t r = 0; r < nfin && k < embed_budget; r++) {
            HChunk *c = &h->f[ref[fin[r].id].file].c[ref[fin[r].id].idx];
            if (!c->etext) continue;
            taken[fin[r].id] = true;
            todo[k] = c;
            tx[k + 1] = c->etext;
            tl[k + 1] = c->elen;
            k++;
        }
        for (size_t i = 0; i < nch && k < embed_budget; i++) { /* then the rest, in file order */
            HChunk *c = &h->f[ref[i].file].c[ref[i].idx];
            if (!c->etext || taken[i]) continue;
            todo[k] = c;
            tx[k + 1] = c->etext;
            tl[k + 1] = c->elen;
            k++;
        }
        free(taken);

        if (embed_texts(h->emb, tx, tl, k + 1) != 0) {
            emb_err = na_strdup(na_errmsg());
        } else {
            for (size_t j = 0; j < k; j++) {
                free(todo[j]->etext);
                todo[j]->etext = NULL;
            }
            embedded_now = k;
        }
        embed_flush(h->emb);
        free(tx);
        free(tl);
        free(todo);
        const float *qv = embed_dim(h->emb) ? embed_get(h->emb, embed_key(h->emb, query, qn)) : NULL;
        if (!qv) continue;
        size_t dim = embed_dim(h->emb);
        for (size_t i = 0; i < nch; i++) {
            const HChunk *c = CH(i);
            const float *v = c->ekey ? embed_get(h->emb, c->ekey) : NULL;
            if (!v) continue;
            double dot = 0;
            for (size_t d = 0; d < dim; d++) dot += (double)qv[d] * v[d];
            ext[i] = dot;
            use_ext = true;
        }
    }


    /* exact phrase: checked on the best candidates only (needs the file text) */
    size_t cand = nfin < 60 ? nfin : 60;
    if (qn >= 3) {
        char *cached = NULL;
        size_t cached_len = 0;
        uint32_t cached_file = (uint32_t)-1;
        for (size_t r = 0; r < cand; r++) {
            const Ref *rf = &ref[fin[r].id];
            const HChunk *c = &h->f[rf->file].c[rf->idx];
            if (rf->file != cached_file) {
                free(cached);
                cached = NULL;
                if (store_snapshot_text(s->st, h->f[rf->file].path, &cached, &cached_len) != 0) {
                    free(cached);
                    cached = NULL;
                }
                cached_file = rf->file;
            }
            if (!cached) continue;
            size_t line = 1, a = 0;
            while (line < c->first && a < cached_len)
                if (cached[a++] == '\n') line++;
            size_t e = a;
            while (line <= c->last && e < cached_len) {
                if (cached[e++] == '\n') line++;
            }
            if (contains_ci(cached + a, e - a, lq, qn)) fin[r].s += 1.0 / 60.0;
        }
        free(cached);
        qsort(fin, cand, sizeof *fin, cmp_rank);
    }

    J *arr = j_arr();
    for (size_t r = 0; r < nfin && r < limit; r++) {
        const Ref *rf = &ref[fin[r].id];
        const HChunk *c = &h->f[rf->file].c[rf->idx];
        const char *rel = h->f[rf->file].path;
        char *text = NULL;
        size_t len;
        if (store_snapshot_text(s->st, rel, &text, &len) != 0) {
            free(text);
            continue;
        }
        /* snippet: the chunk line mentioning most query words */
        size_t line = 1, best_line = c->first, best = 0, pos = 0;
        while (pos <= len && line <= c->last) {
            size_t e = pos;
            while (e < len && text[e] != '\n') e++;
            if (line >= c->first) {
                size_t hits = 0;
                for (size_t w = 0; w < q.n; w++)
                    if (strlen(q.w[w]) >= 3 && contains_ci(text + pos, e - pos, q.w[w], strlen(q.w[w]))) hits++;
                if (hits > best) best = hits, best_line = line;
            }
            if (e >= len) break;
            pos = e + 1;
            line++;
        }
        size_t a = 0, ln = 1;
        while (ln < best_line && a < len)
            if (text[a++] == '\n') ln++;
        size_t e = a;
        while (e < len && text[e] != '\n' && text[e] != '\r') e++;
        if (e - a > 200) {
            e = a + 200;
            while (e > a && ((unsigned char)text[e] & 0xC0) == 0x80) e--;
        }
        J *o = j_obj();
        j_set(o, "path", j_str(rel));
        j_set(o, "line", j_int((int64_t)best_line));
        j_set(o, "text", j_strn(text + a, e - a));
        j_set(o, "start_line", j_int((int64_t)c->first));
        j_set(o, "end_line", j_int((int64_t)c->last));
        if (c->sym) {
            j_set(o, "symbol", j_str(c->sym));
            j_set(o, "kind", j_str(c->kind));
        }
        j_set(o, "score", j_int((int64_t)(fin[r].s * 10000.0 + 0.5)));
        j_push(arr, o);
        free(text);
    }
    J *o = j_obj();
    j_set(o, "count", j_int((int64_t)arr->n));
    j_set(o, "chunks", j_int((int64_t)nch));
    j_set(o, "truncated", j_bool(nfin > limit));
    j_set(o, "matches", arr);
    if (h->emb) {
        J *em = j_obj();
        size_t pending = 0;
        for (size_t i = 0; i < nch; i++)
            if (h->f[ref[i].file].c[ref[i].idx].etext) pending++;
        j_set(em, "model", j_str(embed_model(h->emb)));
        j_set(em, "used", j_bool(use_ext));
        j_set(em, "embedded_now", j_int((int64_t)embedded_now));
        j_set(em, "pending", j_int((int64_t)pending));
        if (emb_err) j_set(em, "error", j_str(emb_err));
        j_set(o, "embedding", em);
    } else if (cfg_err) {
        J *em = j_obj();
        j_set(em, "error", j_str(cfg_err));
        j_set(o, "embedding", em);
    }
    *out = o;
    free(emb_err);
    free(cfg_err);

    free(fin);
    free(score[0]);
    free(score[1]);
    free(sem);
    free(rb);
    free(rn);
    free(re);
    free(rx);
    free(ext);
    free(qid);
    free(ref);
    free(lq);
    words_free(&q);
    return 0;
#undef CH
}
