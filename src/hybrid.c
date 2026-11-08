/* Hybrid code search: BM25 over the identifier words of code chunks, BM25 over symbol and path
 * names, and an exact-phrase signal, fused with reciprocal rank fusion. Chunks follow the symbols
 * of the workspace index (every symbol starts a chunk; long stretches are cut into windows), so a
 * hit is "this function" rather than "this line". Everything is deterministic integer/IEEE
 * arithmetic with no libm, so results are identical on every platform. */
#include "semantic.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW 40
#define MAX_FILE_BYTES (2u << 20)

typedef struct {
    char *key;
    uint32_t *ch, *tf;
    size_t n, cap;
} Term;

typedef struct {
    Term *t;
    size_t n, cap;
    uint32_t *slots; /* term index + 1 */
    size_t scap;
} Inv;

typedef struct {
    uint32_t file, first, last; /* 1-based inclusive lines */
    const Symbol *sym;
    bool phrase;
} Chunk;

static uint64_t fnv(const char *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
    return h;
}

static void inv_free(Inv *v) {
    for (size_t i = 0; i < v->n; i++) {
        free(v->t[i].key);
        free(v->t[i].ch);
        free(v->t[i].tf);
    }
    free(v->t);
    free(v->slots);
    memset(v, 0, sizeof *v);
}

static void inv_rehash(Inv *v) {
    size_t cap = v->scap ? v->scap * 2 : 1024;
    free(v->slots);
    v->slots = na_alloc(cap * sizeof *v->slots);
    memset(v->slots, 0, cap * sizeof *v->slots);
    v->scap = cap;
    for (size_t i = 0; i < v->n; i++) {
        size_t h = fnv(v->t[i].key, strlen(v->t[i].key)) & (cap - 1);
        while (v->slots[h]) h = (h + 1) & (cap - 1);
        v->slots[h] = (uint32_t)i + 1;
    }
}

static Term *inv_find(Inv *v, const char *s, size_t n, bool add) {
    if (v->scap == 0) {
        if (!add) return NULL;
        inv_rehash(v);
    }
    size_t h = fnv(s, n) & (v->scap - 1);
    while (v->slots[h]) {
        Term *t = &v->t[v->slots[h] - 1];
        if (strlen(t->key) == n && memcmp(t->key, s, n) == 0) return t;
        h = (h + 1) & (v->scap - 1);
    }
    if (!add) return NULL;
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 256;
        v->t = na_realloc(v->t, v->cap * sizeof *v->t);
    }
    Term *t = &v->t[v->n];
    memset(t, 0, sizeof *t);
    t->key = na_alloc(n + 1);
    memcpy(t->key, s, n);
    t->key[n] = 0;
    v->slots[h] = (uint32_t)++v->n;
    if (v->n * 2 > v->scap) {
        inv_rehash(v);
        return &v->t[v->n - 1];
    }
    return t;
}

static void inv_add(Inv *v, const char *s, size_t n, uint32_t chunk) {
    Term *t = inv_find(v, s, n, true);
    if (t->n && t->ch[t->n - 1] == chunk) {
        t->tf[t->n - 1]++;
        return;
    }
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 4;
        t->ch = na_realloc(t->ch, t->cap * sizeof *t->ch);
        t->tf = na_realloc(t->tf, t->cap * sizeof *t->tf);
    }
    t->ch[t->n] = chunk;
    t->tf[t->n++] = 1;
}

/* ---- tokens: an identifier yields itself and its camelCase / snake_case words, lower-cased ---- */

typedef void (*Emit)(void *ctx, const char *w, size_t n);

static bool idc(unsigned char c) { return isalnum(c) || c == '_' || c >= 0x80; }

static void ident_words(const char *s, size_t n, Emit emit, void *ctx) {
    char low[96];
    if (n < 2 || n >= sizeof low) {
        if (n < 2) return;
        n = sizeof low - 1; /* absurdly long identifiers: index a prefix */
    }
    size_t pieces = 0, a = 0;
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
            if (b > a) {
                pieces++;
                if (b - a >= 2 && (b - a != n)) emit(ctx, low + a, b - a);
            }
            a = i;
        }
    }
    emit(ctx, low, n); /* the whole identifier, so exact names rank first */
    (void)pieces;
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
    Inv *inv;
    uint32_t chunk;
    size_t *len;
} AddCtx;

static void add_emit(void *c, const char *w, size_t n) {
    AddCtx *a = c;
    inv_add(a->inv, w, n, a->chunk);
    a->len[a->chunk]++;
}

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
    static const char *const s[] = {"the", "of", "to", "in", "for", "is", "and", "how", "does", "where", "what", "with", "an", "or", "do", "it", "on", "by"};
    for (size_t i = 0; i < sizeof s / sizeof *s; i++)
        if (strcmp(w, s[i]) == 0) return true;
    return false;
}

/* ---- deterministic natural log (series, no libm) ---- */
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

typedef struct {
    double s;
    uint32_t id;
} Rank;

static int cmp_rank(const void *a, const void *b) {
    const Rank *x = a, *y = b;
    if (x->s != y->s) return x->s > y->s ? -1 : 1;
    return x->id < y->id ? -1 : x->id > y->id;
}

/* BM25 scores of every chunk for the query words against one field. Returns the number of scored chunks. */
static size_t bm25(Inv *v, const Words *q, const size_t *dl, size_t nch, double k1, double b, double *score) {
    double avg = 0;
    for (size_t i = 0; i < nch; i++) avg += (double)dl[i];
    avg = nch ? avg / (double)nch : 1;
    if (avg < 1) avg = 1;
    size_t hit = 0;
    for (size_t w = 0; w < q->n; w++) {
        Term *t = inv_find(v, q->w[w], strlen(q->w[w]), false);
        if (!t) continue;
        double idf = dlog(1 + ((double)nch - (double)t->n + 0.5) / ((double)t->n + 0.5));
        for (size_t k = 0; k < t->n; k++) {
            double tf = (double)t->tf[k], len = (double)dl[t->ch[k]];
            double add = idf * tf * (k1 + 1) / (tf + k1 * (1 - b + b * len / avg));
            if (score[t->ch[k]] == 0) hit++;
            score[t->ch[k]] += add;
        }
    }
    return hit;
}

static void ranks_of(const double *score, size_t nch, double *rank_out) {
    Rank *r = na_alloc((nch ? nch : 1) * sizeof *r);
    size_t n = 0;
    for (size_t i = 0; i < nch; i++) {
        rank_out[i] = 0;
        if (score[i] > 0) r[n++] = (Rank){score[i], (uint32_t)i};
    }
    qsort(r, n, sizeof *r, cmp_rank);
    for (size_t i = 0; i < n; i++) rank_out[r[i].id] = (double)i + 1; /* 0 = unranked */
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

static void path_words(const char *path, Emit emit, void *ctx) {
    tokenize(path, strlen(path), emit, ctx);
}

int sem_hybrid(Sem *s, const char *query, const char *path_filter, size_t limit, J **out) {
    size_t qn = strlen(query);
    if (qn == 0) return na_fail("invalid parameter: query must not be empty");
    if (limit == 0 || limit > 100) limit = 10;
    if (wsi_refresh(s->ix) != 0) return -1;

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

    Inv body = {0}, name = {0};
    Chunk *ch = NULL;
    size_t nch = 0, cch = 0, *blen = NULL, *nlen = NULL;
    size_t nf = wsi_nfiles(s->ix);
    for (size_t f = 0; f < nf; f++) {
        const char *rel = wsi_path(s->ix, f);
        if (path_filter && *path_filter && !strstr(rel, path_filter)) continue;
        char *text = NULL;
        size_t len;
        if (store_snapshot_text(s->st, rel, &text, &len) != 0 || len > MAX_FILE_BYTES || !utf8_valid(text, len)) {
            free(text);
            continue;
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
        const SymbolList *sl = wsi_symbols(s->ix, f);
        for (size_t i = 0; i < sl->n; i++)
            if (sl->v[i].line >= 1 && sl->v[i].line <= nl) label[sl->v[i].line] = &sl->v[i];

        const Symbol *cur = NULL;
        size_t first = 1;
        for (size_t line = 2; line <= nl + 1; line++) {
            if (!(line == nl + 1 || label[line] || line - first >= WINDOW)) continue;
            if (label[first]) cur = label[first];
            if (nch == cch) {
                cch = cch ? cch * 2 : 256;
                ch = na_realloc(ch, cch * sizeof *ch);
                blen = na_realloc(blen, cch * sizeof *blen);
                nlen = na_realloc(nlen, cch * sizeof *nlen);
            }
            size_t last = line - 1, a = ls[first - 1], e = ls[last];
            ch[nch] = (Chunk){(uint32_t)f, (uint32_t)first, (uint32_t)last, cur, qn >= 3 && contains_ci(text + a, e - a, lq, qn)};
            blen[nch] = nlen[nch] = 0;
            AddCtx bc = {&body, (uint32_t)nch, blen};
            tokenize(text + a, e - a, add_emit, &bc);
            AddCtx nc = {&name, (uint32_t)nch, nlen};
            if (cur) {
                tokenize(cur->name, strlen(cur->name), add_emit, &nc);
                if (cur->scope) tokenize(cur->scope, strlen(cur->scope), add_emit, &nc);
            }
            path_words(rel, add_emit, &nc);
            nch++;
            first = line;
        }
        free(label);
        free(ls);
        free(text);
    }

    double *sb = na_alloc((nch + 1) * sizeof *sb), *sn = na_alloc((nch + 1) * sizeof *sn);
    double *rb = na_alloc((nch + 1) * sizeof *rb), *rn = na_alloc((nch + 1) * sizeof *rn);
    for (size_t i = 0; i < nch; i++) sb[i] = sn[i] = 0;
    bm25(&body, &q, blen, nch, 1.2, 0.75, sb);
    bm25(&name, &q, nlen, nch, 1.2, 0.3, sn);
    ranks_of(sb, nch, rb);
    ranks_of(sn, nch, rn);
    Rank *fin = na_alloc((nch + 1) * sizeof *fin);
    size_t nfin = 0;
    for (size_t i = 0; i < nch; i++) {
        double v = 0;
        if (rb[i] > 0) v += 1.0 / (60.0 + rb[i]);
        if (rn[i] > 0) v += 1.0 / (60.0 + rn[i]);
        if (ch[i].phrase) v += 1.0 / 60.0;
        if (v > 0) fin[nfin++] = (Rank){v, (uint32_t)i};
    }
    qsort(fin, nfin, sizeof *fin, cmp_rank);

    J *arr = j_arr();
    for (size_t r = 0; r < nfin && r < limit; r++) {
        const Chunk *c = &ch[fin[r].id];
        const char *rel = wsi_path(s->ix, c->file);
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
        while (ln < best_line && a < len) {
            if (text[a++] == '\n') ln++;
        }
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
            j_set(o, "symbol", j_str(c->sym->name));
            j_set(o, "kind", j_str(c->sym->kind));
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
    *out = o;

    free(fin);
    free(sb);
    free(sn);
    free(rb);
    free(rn);
    free(ch);
    free(blen);
    free(nlen);
    free(lq);
    words_free(&q);
    inv_free(&body);
    inv_free(&name);
    return 0;
}
