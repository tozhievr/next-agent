#include "store.h"
#include "merge.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define JOURNAL_MAGIC "AEDJ1\n"
#define JOURNAL_MAGIC_LEN 6
#define MAX_RECORD (512u * 1024u * 1024u)
#define LOCK_TIMEOUT_MS 10000

typedef struct {
    size_t doc;
    ChangeSet forward, inverse;
} Delta;

typedef struct {
    int64_t id, parent;
    char *label, *session;
    int64_t ts;
    Delta *d;
    size_t nd;
} Rev;

typedef struct {
    char *path;
    bool has_head;
    int64_t rev;
    char *text;
    size_t len;
    bool base_set, base_exists;
    uint8_t base_hash[16];
    int64_t base_rev; /* document revision whose text is the disk baseline (see store_merge_external) */
    size_t ckpt_bytes; /* delta bytes journaled since the last full-text record */
} Doc;

struct Store {
    char *root, *dir, *journal, *lockpath;
    Doc *docs;
    size_t ndocs, capdocs;
    Rev *revs;
    size_t nrevs, caprevs;
    uint64_t jpos;
};

/* ---------------------------------------------------------------- docs --- */
static Doc *doc_find(Store *st, const char *path) {
    for (size_t i = 0; i < st->ndocs; i++)
        if (strcmp(st->docs[i].path, path) == 0) return &st->docs[i];
    return NULL;
}

static size_t doc_index(Store *st, const char *path) {
    for (size_t i = 0; i < st->ndocs; i++)
        if (strcmp(st->docs[i].path, path) == 0) return i;
    if (st->ndocs == st->capdocs) {
        st->capdocs = st->capdocs ? st->capdocs * 2 : 16;
        st->docs = na_realloc(st->docs, st->capdocs * sizeof *st->docs);
    }
    Doc *d = &st->docs[st->ndocs];
    memset(d, 0, sizeof *d);
    d->path = na_strdup(path);
    return st->ndocs++;
}

/* --------------------------------------------------------- JSON helpers --- */
static J *edits_to_json(const ChangeSet *cs) {
    J *arr = j_arr();
    for (size_t i = 0; i < cs->n; i++) {
        J *e = j_arr();
        j_push(e, j_int((int64_t)cs->v[i].start));
        j_push(e, j_int((int64_t)cs->v[i].end));
        j_push(e, j_strn(cs->v[i].text, cs->v[i].tlen));
        j_push(arr, e);
    }
    return arr;
}

static int edits_from_json(const J *arr, ChangeSet *out) {
    cs_init(out);
    if (!arr || arr->k != J_ARR) return na_fail("corrupt journal: edits");
    for (size_t i = 0; i < arr->n; i++) {
        const J *e = arr->items[i];
        if (e->k != J_ARR || e->n != 3 || !j_is_int(e->items[0]) || !j_is_int(e->items[1]) ||
            !j_is_str(e->items[2]) || e->items[0]->i < 0 || e->items[1]->i < 0) {
            cs_free(out);
            return na_fail("corrupt journal: edit");
        }
        cs_add(out, (size_t)e->items[0]->i, (size_t)e->items[1]->i, e->items[2]->s, e->items[2]->slen);
    }
    return 0;
}

static J *base_to_json(bool exists, const uint8_t hash[16]) {
    J *o = j_obj();
    j_set(o, "exists", j_bool(exists));
    if (exists) {
        char hx[33];
        hex_encode(hash, 16, hx);
        j_set(o, "hash", j_str(hx));
    }
    return o;
}

static int base_from_json(const J *o, Doc *d) {
    const J *ex = j_get(o, "exists");
    const J *hs = j_get(o, "hash");
    if (!ex || ex->k != J_BOOL) return na_fail("corrupt journal: baseline");
    d->base_exists = ex->b;
    memset(d->base_hash, 0, 16);
    if (ex->b) {
        if (!j_is_str(hs) || !hex_decode(hs->s, d->base_hash, 16)) return na_fail("corrupt journal: hash");
    }
    d->base_set = true;
    return 0;
}

/* ------------------------------------------------------- record replay --- */
static void rev_push(Store *st, Rev r) {
    if (st->nrevs == st->caprevs) {
        st->caprevs = st->caprevs ? st->caprevs * 2 : 16;
        st->revs = na_realloc(st->revs, st->caprevs * sizeof *st->revs);
    }
    st->revs[st->nrevs++] = r;
}

static size_t delta_bytes(const ChangeSet *cs) {
    size_t n = 0;
    for (size_t i = 0; i < cs->n; i++) n += cs->v[i].tlen + 16;
    return n;
}

static int apply_record(Store *st, const J *rec) {
    const J *t = j_get(rec, "t");
    if (!j_is_str(t)) return na_fail("corrupt journal: record type");
    if (strcmp(t->s, "base") == 0) {
        const J *p = j_get(rec, "path");
        if (!j_is_str(p)) return na_fail("corrupt journal: base path");
        size_t di = doc_index(st, p->s);
        int brc = base_from_json(rec, &st->docs[di]);
        if (brc == 0) st->docs[di].base_rev = st->docs[di].has_head ? st->docs[di].rev : 0;
        return brc;
    }
    if (strcmp(t->s, "flush") == 0) {
        const J *docs = j_get(rec, "docs");
        if (!docs || docs->k != J_ARR) return na_fail("corrupt journal: flush");
        for (size_t i = 0; i < docs->n; i++) {
            const J *p = j_get(docs->items[i], "path");
            if (!j_is_str(p)) return na_fail("corrupt journal: flush path");
            size_t di = doc_index(st, p->s);
            const J *b = j_get(docs->items[i], "base");
            if (!b || base_from_json(b, &st->docs[di]) != 0) return -1;
            st->docs[di].base_rev = st->docs[di].rev;
        }
        return 0;
    }
    if (strcmp(t->s, "commit") != 0) return na_fail("corrupt journal: unknown record");
    const J *id = j_get(rec, "rev"), *parent = j_get(rec, "parent"), *label = j_get(rec, "label");
    const J *session = j_get(rec, "session"), *ts = j_get(rec, "ts"), *docs = j_get(rec, "docs");
    if (!j_is_int(id) || !j_is_int(ts) || !j_is_str(label) || !j_is_str(session) || !docs ||
        docs->k != J_ARR || id->i != (int64_t)st->nrevs + 1)
        return na_fail("corrupt journal: commit");
    Rev r;
    memset(&r, 0, sizeof r);
    r.id = id->i;
    r.parent = j_is_int(parent) ? parent->i : 0;
    r.label = na_strdup(label->s);
    r.session = na_strdup(session->s);
    r.ts = ts->i;
    r.d = na_alloc((docs->n ? docs->n : 1) * sizeof *r.d);
    for (size_t i = 0; i < docs->n; i++) {
        const J *dj = docs->items[i];
        const J *p = j_get(dj, "path"), *text = j_get(dj, "text");
        if (!j_is_str(p) || (text && !j_is_str(text))) return na_fail("corrupt journal: commit doc");
        size_t di = doc_index(st, p->s);
        Doc *d = &st->docs[di];
        const J *base = j_get(dj, "base");
        if (base && base_from_json(base, d) != 0) return -1;
        if (base) d->base_rev = d->rev;
        Delta *dl = &r.d[r.nd++];
        dl->doc = di;
        if (edits_from_json(j_get(dj, "forward"), &dl->forward) != 0) return -1;
        if (edits_from_json(j_get(dj, "inverse"), &dl->inverse) != 0) return -1;
        if (text) {
            free(d->text);
            d->text = na_strndup(text->s, text->slen);
            d->len = text->slen;
            d->ckpt_bytes = 0;
        } else {
            if (!d->has_head) return na_fail("corrupt journal: delta without a base text for %s", d->path);
            char *nt;
            size_t nl;
            if (cs_apply_text(&dl->forward, d->text, d->len, &nt, &nl) != 0) return -1;
            free(d->text);
            d->text = nt;
            d->len = nl;
            d->ckpt_bytes += delta_bytes(&dl->forward) + delta_bytes(&dl->inverse);
        }
        d->has_head = true;
        d->rev = r.id;
    }
    rev_push(st, r);
    return 0;
}

/* ------------------------------------------------------------- journal --- */
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int refresh(Store *st) {
    Buf b;
    buf_init(&b);
    int rc = pf_read_from(st->journal, st->jpos, &b);
    if (rc < 0) {
        buf_free(&b);
        return -1;
    }
    size_t off = 0;
    if (rc == 0 && st->jpos == 0) {
        if (b.len < JOURNAL_MAGIC_LEN) {
            buf_free(&b);
            return 0; /* freshly created or torn header */
        }
        if (memcmp(b.p, JOURNAL_MAGIC, JOURNAL_MAGIC_LEN) != 0) {
            buf_free(&b);
            return na_fail("corrupt journal: bad header");
        }
        off = JOURNAL_MAGIC_LEN;
    }
    uint64_t consumed = off;
    while (rc == 0 && b.len - off >= 4) {
        const uint8_t *p = (const uint8_t *)b.p + off;
        uint32_t len = rd32(p);
        if (len > MAX_RECORD || b.len - off < (size_t)4 + len + 8) break;
        uint8_t digest[16];
        blake2b16(p + 4, len, digest);
        if (memcmp(digest, p + 4 + len, 8) != 0) break;
        J *rec = json_parse((const char *)p + 4, len);
        if (!rec) {
            buf_free(&b);
            return na_fail("corrupt journal: %s", na_errmsg());
        }
        int arc = apply_record(st, rec);
        json_free(rec);
        if (arc != 0) {
            buf_free(&b);
            return -1;
        }
        off += (size_t)4 + len + 8;
        consumed = off;
    }
    st->jpos += consumed;
    buf_free(&b);
    return 0;
}

static int append_record(Store *st, J *rec) {
    Buf payload, frame;
    buf_init(&payload);
    buf_init(&frame);
    json_write(rec, &payload);
    if (payload.len > MAX_RECORD) {
        buf_free(&payload);
        return na_fail("journal record too large");
    }
    if (st->jpos == 0) buf_put(&frame, JOURNAL_MAGIC, JOURNAL_MAGIC_LEN);
    uint8_t len4[4];
    len4[0] = (uint8_t)(payload.len & 0xFF);
    len4[1] = (uint8_t)((payload.len >> 8) & 0xFF);
    len4[2] = (uint8_t)((payload.len >> 16) & 0xFF);
    len4[3] = (uint8_t)((payload.len >> 24) & 0xFF);
    buf_put(&frame, len4, 4);
    buf_put(&frame, payload.p, payload.len);
    uint8_t digest[16];
    blake2b16(payload.p, payload.len, digest);
    buf_put(&frame, digest, 8);
    int rc = pf_append_at(st->journal, st->jpos, frame.p, frame.len);
    if (rc == 0) {
        st->jpos += frame.len;
        rc = apply_record(st, rec);
    }
    buf_free(&payload);
    buf_free(&frame);
    return rc;
}

static PfLock *begin_write(Store *st) {
    PfLock *l = pf_lock_acquire(st->lockpath, LOCK_TIMEOUT_MS);
    if (!l) return NULL;
    if (refresh(st) != 0) {
        pf_lock_release(l);
        return NULL;
    }
    return l;
}

/* ---------------------------------------------------------- open/close --- */
Store *store_open(const char *root) {
    char *full = pf_fullpath(root);
    if (!full) {
        na_seterr("cannot resolve root %s", root);
        return NULL;
    }
    Store *st = na_alloc(sizeof *st);
    st->root = full;
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/.aed", full);
    st->dir = buf_take(&b);
    buf_printf(&b, "%s/journal.aedj", st->dir);
    st->journal = buf_take(&b);
    buf_printf(&b, "%s/journal.lock", st->dir);
    st->lockpath = buf_take(&b);
    if (pf_mkdirs(st->dir) != 0 || refresh(st) != 0) {
        store_close(st);
        return NULL;
    }
    return st;
}

void store_close(Store *st) {
    if (!st) return;
    for (size_t i = 0; i < st->ndocs; i++) {
        free(st->docs[i].path);
        free(st->docs[i].text);
    }
    free(st->docs);
    for (size_t i = 0; i < st->nrevs; i++) {
        Rev *r = &st->revs[i];
        free(r->label);
        free(r->session);
        for (size_t k = 0; k < r->nd; k++) {
            cs_free(&r->d[k].forward);
            cs_free(&r->d[k].inverse);
        }
        free(r->d);
    }
    free(st->revs);
    free(st->root);
    free(st->dir);
    free(st->journal);
    free(st->lockpath);
    free(st);
}

const char *store_root(const Store *st) { return st->root; }

/* ---------------------------------------------------------------- paths --- */
static bool comp_is(const char *p, size_t cl, const char *name) {
    char tmp[16];
    if (cl != strlen(name) || cl >= sizeof tmp) return false;
    memcpy(tmp, p, cl);
    tmp[cl] = 0;
    return path_equal(tmp, name);
}

char *store_canon(const Store *st, const char *name) {
    if (!name || !*name) {
        na_seterr("path escapes workspace");
        return NULL;
    }
    Buf b;
    buf_init(&b);
    bool absolute = name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':');
    if (!absolute) {
        buf_puts(&b, st->root);
        buf_putc(&b, '/');
    }
    buf_puts(&b, name);
    char *joined = buf_take(&b);
    char *norm = path_normalize(joined);
    free(joined);
    if (!path_inside(st->root, norm)) {
        free(norm);
        na_seterr("path escapes workspace");
        return NULL;
    }
    const char *rel = norm + strlen(st->root);
    while (*rel == '/') rel++;
    /* the two configuration files the tool reads from .aed/ may be written like any other file */
    bool config = strcmp(rel, ".aed/lsp.json") == 0 || strcmp(rel, ".aed/embed.json") == 0;
    /* reserved components and alternate data streams / drive-relative forms */
    for (const char *p = config ? "" : rel; *p;) {
        const char *q = strchr(p, '/');
        size_t cl = q ? (size_t)(q - p) : strlen(p);
        if (comp_is(p, cl, ".git") || comp_is(p, cl, ".aed")) {
            free(norm);
            na_seterr("reserved workspace path");
            return NULL;
        }
        if (memchr(p, ':', cl)) {
            free(norm);
            na_seterr("invalid path component");
            return NULL;
        }
        if (!q) break;
        p = q + 1;
    }
    /* reject symlinks / junctions anywhere below the workspace root */
    Buf walk;
    buf_init(&walk);
    buf_puts(&walk, st->root);
    for (const char *p = rel; *p;) {
        const char *q = strchr(p, '/');
        size_t cl = q ? (size_t)(q - p) : strlen(p);
        buf_putc(&walk, '/');
        buf_put(&walk, p, cl);
        if (pf_is_reparse(walk.p)) {
            buf_free(&walk);
            free(norm);
            na_seterr("symlink escapes workspace");
            return NULL;
        }
        if (!q) break;
        p = q + 1;
    }
    buf_free(&walk);
    char *result = na_strdup(rel);
    free(norm);
    return result;
}

static char *disk_path(const Store *st, const char *canon) {
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", st->root, canon);
    return buf_take(&b);
}

/* ------------------------------------------------------- doc snapshots --- */
typedef struct {
    Snapshot *snap;
    bool disk_exists; /* meaningful when the document has no head */
    uint8_t disk_hash[16];
    bool from_disk;
} Loaded;

static int load_doc(Store *st, const char *canon, Loaded *out) {
    memset(out, 0, sizeof *out);
    Doc *d = doc_find(st, canon);
    if (d && d->has_head) {
        out->snap = snap_new(d->rev, d->text, d->len);
        return 0;
    }
    char *full = disk_path(st, canon);
    Buf b;
    buf_init(&b);
    int rc = pf_read_file(full, &b);
    free(full);
    if (rc < 0) {
        buf_free(&b);
        return -1;
    }
    out->from_disk = true;
    out->disk_exists = rc == 0;
    if (rc == 0) {
        if (!utf8_valid(b.p ? b.p : "", b.len)) {
            buf_free(&b);
            return na_fail("invalid_utf8: %s", canon);
        }
        blake2b16(b.p ? b.p : "", b.len, out->disk_hash);
    }
    out->snap = snap_new(0, b.p ? b.p : "", b.len);
    buf_free(&b);
    return 0;
}

int store_view(Store *st, const char *name, Snapshot **out) {
    char *canon = store_canon(st, name);
    if (!canon) return -1;
    if (refresh(st) != 0) {
        free(canon);
        return -1;
    }
    Doc *d = doc_find(st, canon);
    if (!d || !d->base_set) {
        PfLock *lock = begin_write(st);
        if (!lock) {
            free(canon);
            return -1;
        }
        Loaded ld;
        d = doc_find(st, canon);
        if (!d || !d->has_head) {
            if (load_doc(st, canon, &ld) != 0) {
                pf_lock_release(lock);
                free(canon);
                return -1;
            }
            if (!d || !d->base_set) {
                J *rec = j_obj();
                j_set(rec, "t", j_str("base"));
                j_set(rec, "path", j_str(canon));
                J *base = base_to_json(ld.disk_exists, ld.disk_hash);
                for (size_t i = 0; i < base->n; i++) {
                    j_set(rec, base->keys[i], base->items[i]);
                    base->items[i] = j_null();
                }
                json_free(base);
                int rc = append_record(st, rec);
                json_free(rec);
                if (rc != 0) {
                    snap_free(ld.snap);
                    pf_lock_release(lock);
                    free(canon);
                    return -1;
                }
            }
            *out = ld.snap;
            pf_lock_release(lock);
            free(canon);
            return 0;
        }
        pf_lock_release(lock);
    }
    Loaded ld;
    int rc = load_doc(st, canon, &ld);
    free(canon);
    if (rc != 0) return -1;
    *out = ld.snap;
    return 0;
}

int64_t store_head(Store *st) {
    refresh(st);
    return st->nrevs ? st->revs[st->nrevs - 1].id : 0;
}

int store_snapshot_text(Store *st, const char *name, char **text, size_t *len) {
    char *canon = store_canon(st, name);
    if (!canon) return -1;
    if (refresh(st) != 0) {
        free(canon);
        return -1;
    }
    Loaded ld;
    int rc = load_doc(st, canon, &ld);
    free(canon);
    if (rc != 0) return -1;
    *text = na_strndup(ld.snap->text, ld.snap->len);
    if (len) *len = ld.snap->len;
    snap_free(ld.snap);
    return 0;
}

int store_snapshot_paths(Store *st, StrList *out) {
    if (refresh(st) != 0) return -1;
    for (size_t i = 0; i < st->ndocs; i++)
        if (st->docs[i].has_head) sl_push(out, st->docs[i].path);
    sl_sort(out);
    return 0;
}

void edit_result_free(EditResult *r) { sl_free(&r->paths); }

/* ----------------------------------------------------------------- edit --- */
int store_edit(Store *st, const char *const *names, const ChangeSet *changes, size_t n, int64_t if_rev,
               const char *label, const char *session, const Validator *validator, EditResult *result) {
    if (n == 0) return na_fail("empty edit batch");
    char **canon = na_alloc(n * sizeof *canon);
    char **expected = na_alloc(n * sizeof *expected);
    size_t *expected_len = na_alloc(n * sizeof *expected_len);
    ValidateDoc *vdocs = NULL;
    int rc = -1;
    size_t i;
    for (i = 0; i < n; i++) {
        canon[i] = store_canon(st, names[i]);
        if (!canon[i]) goto done;
    }
    for (i = 0; i < n; i++)
        for (size_t k = i + 1; k < n; k++)
            if (path_equal(canon[i], canon[k])) {
                na_seterr("duplicate canonical paths in edit batch");
                goto done;
            }
    if (refresh(st) != 0) goto done;
    int64_t parent = st->nrevs ? st->revs[st->nrevs - 1].id : 0;
    if (if_rev != STORE_NO_REV && if_rev != parent) {
        na_seterr("stale_rev: expected %lld, current %lld", (long long)if_rev, (long long)parent);
        goto done;
    }
    if (validator && validator->validate) {
        vdocs = na_alloc(n * sizeof *vdocs);
        for (i = 0; i < n; i++) {
            Loaded ld;
            if (load_doc(st, canon[i], &ld) != 0) goto done;
            expected[i] = na_strndup(ld.snap->text, ld.snap->len);
            expected_len[i] = ld.snap->len;
            Snapshot *before = snap_new(parent, ld.snap->text, ld.snap->len);
            snap_free(ld.snap);
            Snapshot *after = NULL;
            if (cs_apply(&changes[i], before, parent + 1, &after) != 0) {
                snap_free(before);
                goto done;
            }
            vdocs[i].path = canon[i];
            vdocs[i].before = before;
            vdocs[i].after = after;
        }
        if (validator->validate(validator->ctx, vdocs, n) != 0) goto done;
    }

    PfLock *lock = begin_write(st);
    if (!lock) goto done;
    int64_t current = st->nrevs ? st->revs[st->nrevs - 1].id : 0;
    if (current != parent || (if_rev != STORE_NO_REV && current != if_rev)) {
        na_seterr("stale_rev: expected %lld, current %lld", (long long)parent, (long long)current);
        pf_lock_release(lock);
        goto done;
    }
    if (validator && validator->recheck && validator->recheck(validator->ctx) != 0) {
        pf_lock_release(lock);
        goto done;
    }
    J *docs = j_arr();
    bool ok = true;
    for (i = 0; i < n && ok; i++) {
        Loaded ld;
        if (load_doc(st, canon[i], &ld) != 0) {
            ok = false;
            break;
        }
        if (validator && validator->validate &&
            (ld.snap->len != expected_len[i] || memcmp(ld.snap->text, expected[i], expected_len[i]) != 0)) {
            na_seterr("stale_snapshot: %s", names[i]);
            snap_free(ld.snap);
            ok = false;
            break;
        }
        Snapshot *after = NULL;
        ChangeSet inverse;
        if (cs_apply(&changes[i], ld.snap, parent + 1, &after) != 0 ||
            cs_invert(&changes[i], ld.snap, &inverse) != 0) {
            snap_free(ld.snap);
            snap_free(after);
            ok = false;
            break;
        }
        J *dj = j_obj();
        j_set(dj, "path", j_str(canon[i]));
        Doc *d = doc_find(st, canon[i]);
        /* Full text only as a checkpoint: a document's first revision, or once the journaled
         * deltas outweigh the text. Replay stays bounded and writes are amortized O(delta). */
        if (!d || !d->has_head || d->ckpt_bytes + delta_bytes(&changes[i]) + delta_bytes(&inverse) > d->len)
            j_set(dj, "text", j_strn(after->text, after->len));
        j_set(dj, "forward", edits_to_json(&changes[i]));
        j_set(dj, "inverse", edits_to_json(&inverse));
        if (!d || !d->base_set) j_set(dj, "base", base_to_json(ld.disk_exists, ld.disk_hash));
        j_push(docs, dj);
        cs_free(&inverse);
        snap_free(after);
        snap_free(ld.snap);
    }
    if (ok) {
        J *rec = j_obj();
        j_set(rec, "t", j_str("commit"));
        j_set(rec, "rev", j_int(parent + 1));
        j_set(rec, "parent", parent ? j_int(parent) : j_null());
        j_set(rec, "label", j_str(label ? label : "edit"));
        j_set(rec, "session", j_str(session ? session : "cli"));
        j_set(rec, "ts", j_int((int64_t)time(NULL)));
        j_set(rec, "docs", docs);
        docs = NULL;
        ok = append_record(st, rec) == 0;
        json_free(rec);
    }
    json_free(docs);
    pf_lock_release(lock);
    if (!ok) goto done;
    if (result) {
        result->rev = parent + 1;
        sl_init(&result->paths);
        for (i = 0; i < n; i++) sl_push(&result->paths, names[i]);
    }
    rc = 0;
done:
    for (i = 0; i < n; i++) {
        free(canon[i]);
        free(expected[i]);
        if (vdocs) {
            snap_free((Snapshot *)vdocs[i].before);
            snap_free((Snapshot *)vdocs[i].after);
        }
    }
    free(canon);
    free(expected);
    free(expected_len);
    free(vdocs);
    return rc;
}

/* ----------------------------------------------------------- undo/redo --- */
/* Shift/inspect `edits` (coordinates after revision `r`) through later changes. */
static int rebase_through(Store *st, const Rev *r, size_t doc, ChangeSet *edits) {
    for (size_t k = (size_t)r->id; k < st->nrevs; k++) {
        const Rev *later = &st->revs[k];
        for (size_t di = 0; di < later->nd; di++) {
            if (later->d[di].doc != doc) continue;
            const ChangeSet *f = &later->d[di].forward;
            for (size_t ei = 0; ei < edits->n; ei++) {
                Edit *e = &edits->v[ei];
                int64_t shift = 0;
                for (size_t fi = 0; fi < f->n; fi++) {
                    const Edit *fe = &f->v[fi];
                    if (fe->start <= e->end && fe->end >= e->start)
                        return na_fail("undo_conflict: revision %lld overlaps later revision %lld in %s",
                                       (long long)r->id, (long long)later->id, st->docs[doc].path);
                    if (fe->end < e->start)
                        shift += (int64_t)utf8_cp_count(fe->text, fe->tlen) - (int64_t)(fe->end - fe->start);
                }
                e->start = (size_t)((int64_t)e->start + shift);
                e->end = (size_t)((int64_t)e->end + shift);
            }
        }
    }
    return 0;
}

int store_undo(Store *st, int64_t rev, const Validator *validator, EditResult *result) {
    if (refresh(st) != 0) return -1;
    int64_t head = st->nrevs ? st->revs[st->nrevs - 1].id : 0;
    if (!head) return na_fail("no revisions to undo");
    if (rev == 0) rev = head;
    if (rev < 1 || rev > head) return na_fail("unknown revision %lld", (long long)rev);
    const Rev *r = &st->revs[rev - 1];
    if (r->nd == 0) return na_fail("revision has no changes");
    const char **names = na_alloc(r->nd * sizeof *names);
    ChangeSet *changes = na_alloc(r->nd * sizeof *changes);
    size_t made = 0;
    int rc = -1;
    for (size_t i = 0; i < r->nd; i++) {
        names[i] = st->docs[r->d[i].doc].path;
        changes[i] = cs_clone(&r->d[i].inverse);
        made++;
        if (rev != head && rebase_through(st, r, r->d[i].doc, &changes[i]) != 0) goto out;
    }
    char label[64];
    snprintf(label, sizeof label, "undo revision %lld", (long long)rev);
    rc = store_edit(st, names, changes, r->nd, head, label, "cli", validator, result);
out:
    for (size_t i = 0; i < made; i++) cs_free(&changes[i]);
    free(changes);
    free(names);
    return rc;
}

int store_redo(Store *st, const Validator *validator, EditResult *result) {
    if (refresh(st) != 0) return -1;
    if (!st->nrevs) return na_fail("nothing to redo");
    const Rev *head = &st->revs[st->nrevs - 1];
    if (strncmp(head->label, "undo revision ", 14) != 0) return na_fail("nothing to redo");
    return store_undo(st, head->id, validator, result);
}

J *store_history(Store *st, int limit) {
    refresh(st);
    J *arr = j_arr();
    for (size_t k = st->nrevs; k > 0 && limit > 0; k--, limit--) {
        const Rev *r = &st->revs[k - 1];
        J *o = j_obj();
        j_set(o, "rev", j_int(r->id));
        j_set(o, "parent", r->parent ? j_int(r->parent) : j_null());
        j_set(o, "label", j_str(r->label));
        j_set(o, "ts", j_int(r->ts));
        j_push(arr, o);
    }
    return arr;
}

/* ---------------------------------------------------------------- flush --- */
static int disk_state(const char *full, bool *exists, uint8_t hash[16]) {
    Buf b;
    buf_init(&b);
    int rc = pf_read_file(full, &b);
    if (rc < 0) {
        buf_free(&b);
        return -1;
    }
    *exists = rc == 0;
    if (rc == 0) blake2b16(b.p ? b.p : "", b.len, hash);
    buf_free(&b);
    return 0;
}

static bool disk_conflict(const Doc *d, bool exists, const uint8_t hash[16]) {
    if (exists != d->base_exists) return true;
    return exists && memcmp(hash, d->base_hash, 16) != 0;
}

/* ------------------------------------------------- external changes --- */
/* The text the document had when the disk baseline was taken: the head text with the inverse deltas of
 * every later revision applied, newest first. */
static int doc_base_text(Store *st, const Doc *d, char **out, size_t *outlen) {
    char *text = na_strndup(d->text ? d->text : "", d->len);
    size_t len = d->len, di = (size_t)(d - st->docs);
    for (int64_t r = (int64_t)st->nrevs; r > d->base_rev; r--) {
        const Rev *rv = &st->revs[r - 1];
        for (size_t k = 0; k < rv->nd; k++) {
            if (rv->d[k].doc != di) continue;
            char *nt;
            size_t nl;
            if (cs_apply_text(&rv->d[k].inverse, text, len, &nt, &nl) != 0) {
                free(text);
                return -1;
            }
            free(text);
            text = nt;
            len = nl;
        }
    }
    *out = text;
    *outlen = len;
    return 0;
}

static int record_baseline(Store *st, const char *canon, bool exists, const uint8_t hash[16]) {
    PfLock *lock = begin_write(st);
    if (!lock) return -1;
    J *rec = j_obj(), *docs = j_arr(), *e = j_obj();
    j_set(rec, "t", j_str("flush"));
    j_set(e, "path", j_str(canon));
    j_set(e, "base", base_to_json(exists, hash));
    j_push(docs, e);
    j_set(rec, "docs", docs);
    int rc = append_record(st, rec);
    json_free(rec);
    pf_lock_release(lock);
    return rc;
}

/* Three-way reconciliation of a document whose file was changed outside NextAgent (the situation in
 * which `sync` reports disk_conflict). strategy: "auto" merges base/ours/disk by lines and reports the
 * conflicts when it cannot; "theirs" takes the disk text; "ours" keeps the journal text and makes the
 * next flush overwrite the disk. The report has `status`: unchanged | fast_forward | merged | conflict |
 * took_theirs | kept_ours, plus `conflicts` and, when the journal changed, `rev`/`paths`. */
int store_merge_external(Store *st, const char *name, const char *strategy, bool dry_run, J **report) {
    if (!strategy) strategy = "auto";
    if (strcmp(strategy, "auto") != 0 && strcmp(strategy, "ours") != 0 && strcmp(strategy, "theirs") != 0)
        return na_fail("invalid parameter: strategy must be auto, ours or theirs");
    char *canon = store_canon(st, name);
    if (!canon) return -1;
    if (refresh(st) != 0) {
        free(canon);
        return -1;
    }
    J *rep = j_obj();
    j_set(rep, "path", j_str(canon));
    Doc *d = doc_find(st, canon);
    char *full = disk_path(st, canon);
    bool exists = false;
    uint8_t hash[16];
    int rc = 0;
    if (!d || !d->has_head || !d->base_set || disk_state(full, &exists, hash) != 0 || !disk_conflict(d, exists, hash)) {
        j_set(rep, "status", j_str("unchanged"));
        goto done;
    }
    if (strcmp(strategy, "ours") == 0 || !exists) {
        if (!exists && strcmp(strategy, "ours") != 0) {
            j_set(rep, "status", j_str("conflict"));
            j_set(rep, "message", j_str("the file was deleted on disk; use strategy ours to recreate it"));
            goto done;
        }
        if (!dry_run) rc = record_baseline(st, canon, exists, hash);
        j_set(rep, "status", j_str("kept_ours"));
        goto done_paths;
    }
    Buf disk;
    buf_init(&disk);
    if (pf_read_file(full, &disk) != 0) {
        buf_free(&disk);
        rc = na_fail("cannot read %s", canon);
        goto done;
    }
    if (!utf8_valid(disk.p ? disk.p : "", disk.len)) {
        buf_free(&disk);
        rc = na_fail("invalid_utf8: %s on disk is not valid UTF-8", canon);
        goto done;
    }
    char *base = NULL;
    size_t blen = 0;
    if (doc_base_text(st, d, &base, &blen) != 0) {
        buf_free(&disk);
        rc = -1;
        goto done;
    }
    const char *status = "merged";
    Buf merged;
    buf_init(&merged);
    const char *newtext = disk.p ? disk.p : "";
    size_t newlen = disk.len;
    if (strcmp(strategy, "theirs") == 0) {
        status = "took_theirs";
    } else if (blen == d->len && (blen == 0 || memcmp(base, d->text, blen) == 0)) {
        status = "fast_forward"; /* nothing changed locally since the baseline */
    } else {
        MergeConflicts conf;
        int mrc = merge3(base, blen, d->text ? d->text : "", d->len, newtext, newlen, &merged, &conf);
        if (mrc < 0) {
            rc = na_fail("merge failed");
        } else if (mrc > 0) {
            J *arr = j_arr();
            for (size_t i = 0; i < conf.n; i++) {
                J *c = j_obj();
                j_set(c, "first_line", j_int((int64_t)conf.v[i].first));
                j_set(c, "last_line", j_int((int64_t)conf.v[i].last));
                j_set(c, "ours", j_str(conf.v[i].ours));
                j_set(c, "theirs", j_str(conf.v[i].theirs));
                j_push(arr, c);
            }
            j_set(rep, "status", j_str("conflict"));
            j_set(rep, "conflicts", arr);
            status = NULL;
        } else {
            newtext = merged.p ? merged.p : "";
            newlen = merged.len;
        }
        merge_conflicts_free(&conf);
    }
    if (rc == 0 && status) {
        j_set(rep, "status", j_str(status));
        if (!dry_run) {
            /* replace only the differing middle of the head text */
            const char *head = d->text ? d->text : "";
            size_t pre = 0, suf = 0, hl = d->len;
            while (pre < hl && pre < newlen && head[pre] == newtext[pre]) pre++;
            while (pre > 0 && pre < hl && ((unsigned char)head[pre] & 0xC0) == 0x80) pre--;
            while (suf < hl - pre && suf < newlen - pre && head[hl - 1 - suf] == newtext[newlen - 1 - suf]) suf++;
            while (suf > 0 && ((unsigned char)head[hl - suf] & 0xC0) == 0x80) suf--;
            if (pre + suf < hl || pre + suf < newlen) {
                ChangeSet cs;
                cs_init(&cs);
                size_t a = utf8_cp_count(head, pre);
                cs_add(&cs, a, a + utf8_cp_count(head + pre, hl - pre - suf), newtext + pre, newlen - pre - suf);
                const char *names[1] = {canon};
                EditResult res;
                rc = store_edit(st, names, &cs, 1, (int64_t)st->nrevs, "merge external changes", "merge", NULL, &res);
                cs_free(&cs);
                if (rc == 0) {
                    j_set(rep, "rev", j_int(res.rev));
                    edit_result_free(&res);
                }
            }
            if (rc == 0) rc = record_baseline(st, canon, true, hash);
        }
    }
    free(base);
    buf_free(&merged);
    buf_free(&disk);
done_paths:
    if (rc == 0 && !dry_run && j_get(rep, "status") && strcmp(j_get(rep, "status")->s, "conflict") != 0) {
        J *paths = j_arr();
        j_push(paths, j_str(canon));
        j_set(rep, "paths", paths);
    }
done:
    free(full);
    free(canon);
    if (rc != 0) {
        json_free(rep);
        return -1;
    }
    *report = rep;
    return 0;
}

int store_flush(Store *st, const char *const *paths, size_t npaths, StrList *written) {
    PfLock *lock = begin_write(st);
    if (!lock) return -1;
    StrList wanted;
    sl_init(&wanted);
    int rc = -1;
    if (paths) {
        for (size_t i = 0; i < npaths; i++) {
            char *c = store_canon(st, paths[i]);
            if (!c) goto out;
            sl_push(&wanted, c);
            free(c);
        }
    }
    StrList selected;
    sl_init(&selected);
    for (size_t i = 0; i < st->ndocs; i++) {
        const Doc *d = &st->docs[i];
        if (!d->has_head || !d->base_set) continue;
        if (paths && !sl_has(&wanted, d->path)) continue;
        sl_push(&selected, d->path);
    }
    sl_sort(&selected);
    /* detect conflicts across ALL selected paths before writing any; a flush of everything (sync without paths)
     * skips conflicted files, writes the rest and reports the skipped ones at the end */
    char *skipped = NULL;
    size_t nskipped = 0;
    for (size_t i = 0; i < selected.n;) {
        const Doc *d = doc_find(st, selected.v[i]);
        char *full = disk_path(st, d->path);
        bool exists = false;
        uint8_t hash[16];
        int drc = disk_state(full, &exists, hash);
        free(full);
        if (drc != 0) goto out_sel;
        if (disk_conflict(d, exists, hash)) {
            if (paths) {
                na_seterr("disk_conflict: external modification of %s (reconcile with: nextagent merge %s)", d->path, d->path);
                goto out_sel;
            }
            if (!skipped) skipped = na_strdup(d->path);
            nskipped++;
            free(selected.v[i]);
            memmove(selected.v + i, selected.v + i + 1, (selected.n - i - 1) * sizeof *selected.v);
            selected.n--;
            continue;
        }
        i++;
    }
    for (size_t i = 0; i < selected.n; i++) {
        const Doc *d = doc_find(st, selected.v[i]);
        char *full = disk_path(st, d->path);
        bool exists = false;
        uint8_t hash[16];
        if (disk_state(full, &exists, hash) != 0) {
            free(full);
            goto out_sel;
        }
        if (disk_conflict(d, exists, hash)) {
            na_seterr("disk_conflict: external modification of %s (reconcile with: nextagent merge %s)", d->path, d->path);
            free(full);
            goto out_sel;
        }
        char *slash = strrchr(full, '/');
        if (slash) {
            *slash = 0;
            int mrc = pf_mkdirs(full);
            *slash = '/';
            if (mrc != 0) {
                free(full);
                goto out_sel;
            }
        }
        if (pf_write_atomic(full, d->text ? d->text : "", d->len) != 0) {
            free(full);
            goto out_sel;
        }
        free(full);
        uint8_t nh[16];
        blake2b16(d->text ? d->text : "", d->len, nh);
        J *rec = j_obj(), *docs = j_arr(), *e = j_obj();
        j_set(rec, "t", j_str("flush"));
        j_set(e, "path", j_str(d->path));
        j_set(e, "base", base_to_json(true, nh));
        j_push(docs, e);
        j_set(rec, "docs", docs);
        int arc = append_record(st, rec);
        json_free(rec);
        if (arc != 0) goto out_sel;
        if (written) sl_push(written, selected.v[i]);
    }
    rc = 0;
    if (nskipped) {
        na_seterr("disk_conflict: %zu file(s) changed outside nextagent were not written, the others were (first: %s; reconcile with: nextagent merge %s)",
                  nskipped, skipped, skipped);
        rc = -1;
    }
out_sel:
    free(skipped);
    sl_free(&selected);
out:
    sl_free(&wanted);
    pf_lock_release(lock);
    return rc;
}
