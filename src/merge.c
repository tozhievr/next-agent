#include "merge.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    size_t n;
} Line;

typedef struct {
    Line *v;
    size_t n;
} Lines;

typedef struct {
    size_t from, to;   /* base lines [from, to) */
    size_t bfrom, bto; /* replacement: lines [bfrom, bto) of the other side */
} Hunk;

typedef struct {
    Hunk *v;
    size_t n, cap;
} Hunks;

#define MAX_CELLS ((size_t)16 << 20)

static void split(const char *t, size_t n, Lines *out) {
    size_t cap = 64;
    out->v = na_alloc(cap * sizeof *out->v);
    out->n = 0;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && t[j] != '\n') j++;
        if (j < n) j++; /* keep the terminator: a missing final newline is a difference */
        if (out->n == cap) {
            cap *= 2;
            out->v = na_realloc(out->v, cap * sizeof *out->v);
        }
        out->v[out->n++] = (Line){t + i, j - i};
        i = j;
    }
}

static bool same(const Line *a, const Line *b) { return a->n == b->n && memcmp(a->p, b->p, a->n) == 0; }

static void hunk_push(Hunks *h, Hunk x) {
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 16;
        h->v = na_realloc(h->v, h->cap * sizeof *h->v);
    }
    h->v[h->n++] = x;
}

/* hunks that turn lines `a` into lines `b`; 0 ok, -1 when the differing middle is too large for the LCS table */
static int diff(const Lines *a, const Lines *b, Hunks *out) {
    size_t pre = 0;
    while (pre < a->n && pre < b->n && same(&a->v[pre], &b->v[pre])) pre++;
    size_t suf = 0;
    while (suf < a->n - pre && suf < b->n - pre && same(&a->v[a->n - 1 - suf], &b->v[b->n - 1 - suf])) suf++;
    size_t an = a->n - pre - suf, bn = b->n - pre - suf;
    if (an == 0 && bn == 0) return 0;
    if (an == 0 || bn == 0) {
        hunk_push(out, (Hunk){pre, pre + an, pre, pre + bn});
        return 0;
    }
    if ((an + 1) * (bn + 1) > MAX_CELLS) {
        hunk_push(out, (Hunk){pre, pre + an, pre, pre + bn}); /* coarse: the whole middle changed */
        return 0;
    }
    uint32_t *t = na_alloc((an + 1) * (bn + 1) * sizeof *t);
#define T(i, j) t[(i) * (bn + 1) + (j)]
    for (size_t i = an; i-- > 0;)
        for (size_t j = bn; j-- > 0;)
            T(i, j) = same(&a->v[pre + i], &b->v[pre + j]) ? T(i + 1, j + 1) + 1 : (T(i + 1, j) >= T(i, j + 1) ? T(i + 1, j) : T(i, j + 1));
    size_t i = 0, j = 0;
    while (i < an || j < bn) {
        if (i < an && j < bn && same(&a->v[pre + i], &b->v[pre + j])) {
            i++;
            j++;
            continue;
        }
        size_t si = i, sj = j;
        while ((i < an || j < bn) && !(i < an && j < bn && same(&a->v[pre + i], &b->v[pre + j]))) {
            if (j >= bn || (i < an && T(i + 1, j) >= T(i, j + 1))) i++;
            else j++;
        }
        hunk_push(out, (Hunk){pre + si, pre + i, pre + sj, pre + j});
    }
#undef T
    free(t);
    return 0;
}

static void put_lines(Buf *b, const Lines *l, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) buf_put(b, l->v[i].p, l->v[i].n);
}

/* base [from,to) with the hunks h[first..last) of one side applied */
static void side_text(Buf *b, const Lines *base, const Lines *side, const Hunks *h, size_t first, size_t last, size_t from, size_t to) {
    size_t pos = from;
    for (size_t k = first; k < last; k++) {
        put_lines(b, base, pos, h->v[k].from);
        put_lines(b, side, h->v[k].bfrom, h->v[k].bto);
        pos = h->v[k].to;
    }
    put_lines(b, base, pos, to);
}

int merge3(const char *base, size_t bn, const char *ours, size_t on, const char *theirs, size_t tn, Buf *out, MergeConflicts *conf) {
    Lines lb, lo, lt;
    split(base, bn, &lb);
    split(ours, on, &lo);
    split(theirs, tn, &lt);
    Hunks ho = {0}, ht = {0};
    int rc = diff(&lb, &lo, &ho) == 0 && diff(&lb, &lt, &ht) == 0 ? 0 : -1;
    memset(conf, 0, sizeof *conf);
    size_t cap = 0;
    if (rc == 0) {
        size_t oi = 0, ti = 0, pos = 0;
        while (oi < ho.n || ti < ht.n) {
            const Hunk *o = oi < ho.n ? &ho.v[oi] : NULL, *t = ti < ht.n ? &ht.v[ti] : NULL;
            if (o && t && o->from <= t->to && t->from <= o->to) { /* touching or overlapping: one region */
                size_t from = o->from < t->from ? o->from : t->from, to = o->to > t->to ? o->to : t->to;
                size_t oj = oi + 1, tj = ti + 1;
                for (bool grew = true; grew;) {
                    grew = false;
                    if (oj < ho.n && ho.v[oj].from <= to) {
                        if (ho.v[oj].to > to) to = ho.v[oj].to;
                        oj++;
                        grew = true;
                    }
                    if (tj < ht.n && ht.v[tj].from <= to) {
                        if (ht.v[tj].to > to) to = ht.v[tj].to;
                        tj++;
                        grew = true;
                    }
                }
                put_lines(out, &lb, pos, from);
                Buf a, b;
                buf_init(&a);
                buf_init(&b);
                side_text(&a, &lb, &lo, &ho, oi, oj, from, to);
                side_text(&b, &lb, &lt, &ht, ti, tj, from, to);
                if (a.len == b.len && (a.len == 0 || memcmp(a.p, b.p, a.len) == 0)) {
                    buf_put(out, a.p ? a.p : "", a.len); /* both made the same change */
                } else {
                    if (conf->n == cap) {
                        cap = cap ? cap * 2 : 4;
                        conf->v = na_realloc(conf->v, cap * sizeof *conf->v);
                    }
                    conf->v[conf->n++] = (MergeConflict){from + 1, to, buf_take(&a), buf_take(&b)};
                    a.p = b.p = NULL;
                }
                buf_free(&a);
                buf_free(&b);
                pos = to;
                oi = oj;
                ti = tj;
            } else if (o && (!t || o->from < t->from)) {
                put_lines(out, &lb, pos, o->from);
                put_lines(out, &lo, o->bfrom, o->bto);
                pos = o->to;
                oi++;
            } else {
                put_lines(out, &lb, pos, t->from);
                put_lines(out, &lt, t->bfrom, t->bto);
                pos = t->to;
                ti++;
            }
        }
        put_lines(out, &lb, pos, lb.n);
        rc = conf->n ? 1 : 0;
    }
    free(lb.v);
    free(lo.v);
    free(lt.v);
    free(ho.v);
    free(ht.v);
    return rc;
}

void merge_conflicts_free(MergeConflicts *c) {
    for (size_t i = 0; i < c->n; i++) {
        free(c->v[i].ours);
        free(c->v[i].theirs);
    }
    free(c->v);
    memset(c, 0, sizeof *c);
}
