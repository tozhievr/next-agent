#include "core.h"
#include <stdlib.h>
#include <string.h>

size_t cp_to_byte(const char *text, size_t len, size_t cp) {
    size_t b = 0, c = 0;
    while (c < cp) {
        if (b >= len) return (size_t)-1;
        size_t l = utf8_seq_len((unsigned char)text[b]);
        b += l ? l : 1;
        c++;
    }
    return b;
}

Snapshot *snap_new(int64_t rev, const char *text, size_t len) {
    Snapshot *s = na_alloc(sizeof *s);
    s->rev = rev;
    s->text = na_strndup(text, len);
    s->len = len;
    s->ncp = utf8_cp_count(text, len);
    blake2b16(text, len, s->hash);
    size_t lines = 1;
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n') lines++;
    s->line_starts = na_alloc(lines * sizeof *s->line_starts);
    s->nlines = lines;
    size_t li = 1, cp = 0;
    s->line_starts[0] = 0;
    for (size_t i = 0; i < len; i++) {
        if (((unsigned char)text[i] & 0xC0) == 0x80) continue;
        cp++;
        if (text[i] == '\n') s->line_starts[li++] = cp;
    }
    return s;
}

void snap_free(Snapshot *s) {
    if (!s) return;
    free(s->text);
    free(s->line_starts);
    free(s);
}

int snap_position(const Snapshot *s, size_t offset, size_t *line, size_t *col) {
    if (offset > s->ncp) return na_fail("offset outside document");
    size_t lo = 0, hi = s->nlines; /* last line_start <= offset */
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (s->line_starts[mid] <= offset) lo = mid;
        else hi = mid;
    }
    *line = lo + 1;
    *col = offset - s->line_starts[lo];
    return 0;
}

int snap_offset(const Snapshot *s, size_t line, size_t col, size_t *offset) {
    if (line < 1 || line > s->nlines) return na_fail("invalid line or column");
    size_t start = s->line_starts[line - 1];
    size_t end = line < s->nlines ? s->line_starts[line] : s->ncp;
    if (start + col > end) return na_fail("column exceeds line");
    *offset = start + col;
    return 0;
}

void cs_init(ChangeSet *cs) {
    cs->v = NULL;
    cs->n = cs->cap = 0;
}

void cs_free(ChangeSet *cs) {
    for (size_t i = 0; i < cs->n; i++) free(cs->v[i].text);
    free(cs->v);
    cs_init(cs);
}

void cs_add(ChangeSet *cs, size_t start, size_t end, const char *text, size_t tlen) {
    if (cs->n == cs->cap) {
        cs->cap = cs->cap ? cs->cap * 2 : 4;
        cs->v = na_realloc(cs->v, cs->cap * sizeof *cs->v);
    }
    Edit *e = &cs->v[cs->n++];
    e->start = start;
    e->end = end;
    e->text = na_strndup(text, tlen);
    e->tlen = tlen;
}

/* Insertion sort keeps the sort stable, as Python's sorted() is. */
void cs_sort(ChangeSet *cs) {
    for (size_t i = 1; i < cs->n; i++) {
        Edit key = cs->v[i];
        size_t j = i;
        while (j > 0 && (cs->v[j - 1].start > key.start ||
                         (cs->v[j - 1].start == key.start && cs->v[j - 1].end > key.end))) {
            cs->v[j] = cs->v[j - 1];
            j--;
        }
        cs->v[j] = key;
    }
}

int cs_validate(const ChangeSet *cs) {
    bool have_prev = false;
    size_t prev_end = 0;
    for (size_t i = 0; i < cs->n; i++) {
        const Edit *e = &cs->v[i];
        if (e->end < e->start) return na_fail("invalid edit range");
        if (have_prev && (e->start < prev_end || (e->start == prev_end && e->start == e->end)))
            return na_fail("edits must be sorted and non-overlapping");
        prev_end = e->end;
        have_prev = true;
    }
    return 0;
}

ChangeSet cs_clone(const ChangeSet *cs) {
    ChangeSet r;
    cs_init(&r);
    for (size_t i = 0; i < cs->n; i++) cs_add(&r, cs->v[i].start, cs->v[i].end, cs->v[i].text, cs->v[i].tlen);
    return r;
}

int cs_apply(const ChangeSet *cs, const Snapshot *s, int64_t new_rev, Snapshot **out) {
    Buf b;
    buf_init(&b);
    size_t cur_cp = 0, cur_b = 0;
    for (size_t i = 0; i < cs->n; i++) {
        const Edit *e = &cs->v[i];
        if (e->end > s->ncp) {
            buf_free(&b);
            return na_fail("edit beyond document end");
        }
        size_t sb = cur_b, sc = cur_cp;
        while (sc < e->start) {
            size_t l = utf8_seq_len((unsigned char)s->text[sb]);
            sb += l ? l : 1;
            sc++;
        }
        buf_put(&b, s->text + cur_b, sb - cur_b);
        buf_put(&b, e->text, e->tlen);
        size_t eb = sb, ec = sc;
        while (ec < e->end) {
            size_t l = utf8_seq_len((unsigned char)s->text[eb]);
            eb += l ? l : 1;
            ec++;
        }
        cur_b = eb;
        cur_cp = ec;
    }
    buf_put(&b, s->text + cur_b, s->len - cur_b);
    *out = snap_new(new_rev, b.p ? b.p : "", b.len);
    buf_free(&b);
    return 0;
}

int cs_invert(const ChangeSet *cs, const Snapshot *s, ChangeSet *out) {
    cs_init(out);
    int64_t disp = 0;
    size_t cur_cp = 0, cur_b = 0;
    for (size_t i = 0; i < cs->n; i++) {
        const Edit *e = &cs->v[i];
        if (e->end > s->ncp) {
            cs_free(out);
            return na_fail("edit beyond document end");
        }
        size_t sb = cur_b, sc = cur_cp;
        while (sc < e->start) {
            size_t l = utf8_seq_len((unsigned char)s->text[sb]);
            sb += l ? l : 1;
            sc++;
        }
        size_t eb = sb, ec = sc;
        while (ec < e->end) {
            size_t l = utf8_seq_len((unsigned char)s->text[eb]);
            eb += l ? l : 1;
            ec++;
        }
        size_t start = (size_t)((int64_t)e->start + disp);
        size_t new_cp_len = utf8_cp_count(e->text, e->tlen);
        cs_add(out, start, start + new_cp_len, s->text + sb, eb - sb);
        disp += (int64_t)new_cp_len - (int64_t)(e->end - e->start);
        cur_b = eb;
        cur_cp = ec;
    }
    return 0;
}

size_t cs_map_pos(const ChangeSet *cs, size_t offset, int assoc) {
    int64_t disp = 0;
    for (size_t i = 0; i < cs->n; i++) {
        const Edit *e = &cs->v[i];
        size_t new_len = utf8_cp_count(e->text, e->tlen);
        if (offset < e->start || (offset == e->start && assoc < 0)) break;
        if (offset <= e->end) return (size_t)((int64_t)e->start + disp + (assoc > 0 ? (int64_t)new_len : 0));
        disp += (int64_t)new_len - (int64_t)(e->end - e->start);
    }
    return (size_t)((int64_t)offset + disp);
}

int cs_stats(const ChangeSet *cs, const Snapshot *s, size_t *added, size_t *removed) {
    *added = *removed = 0;
    for (size_t i = 0; i < cs->n; i++) {
        const Edit *e = &cs->v[i];
        if (e->end > s->ncp) return na_fail("edit beyond document end");
        for (size_t k = 0; k < e->tlen; k++)
            if (e->text[k] == '\n') (*added)++;
        size_t sb = cp_to_byte(s->text, s->len, e->start), eb = cp_to_byte(s->text, s->len, e->end);
        for (size_t k = sb; k < eb; k++)
            if (s->text[k] == '\n') (*removed)++;
    }
    return 0;
}
