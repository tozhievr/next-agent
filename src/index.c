#include "index.h"
#include "platform.h"
#include "bin.h"
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 20000

typedef struct {
    char *path;
    uint64_t hash;
    size_t len;
    SymbolList syms;
    bool server;
    uint32_t *ids; /* sorted unique identifier ids */
    size_t nids;
    bool seen;
    PfStat sig;      /* size + mtime when the content was last read from disk */
    int64_t sig_at;  /* wall clock of that read; the signature is trusted only when the file was already older than 2 s */
    bool has_sig;
} Entry;

static int cmp_key(const void *k, const void *e) { return strcmp((const char *)k, ((const Entry *)e)->path); }

typedef struct {
    char **names;
    uint32_t *slots; /* id + 1, 0 = empty */
    size_t cap, n;
} Interner;

struct WsIndex {
    Store *st;
    LspHub *hub;
    Entry *v;
    size_t n, cap;
    Interner in;
    size_t extracted, skipped;
    void *user;
    void (*user_free)(void *);
    bool cache_tried, dirty; /* .aed/index.cache was looked at / the in-memory index differs from it */
    char *blob;              /* the hybrid-search section of the cache file, handed to the search on its first use */
    size_t blob_n;
};

static uint64_t fnv(const char *p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ull;
    return h;
}

static void in_grow(Interner *in) {
    size_t ncap = in->cap ? in->cap * 2 : 1024;
    uint32_t *slots = na_alloc(ncap * sizeof *slots);
    for (size_t i = 0; i < in->n; i++) {
        size_t k = fnv(in->names[i], strlen(in->names[i])) & (ncap - 1);
        while (slots[k]) k = (k + 1) & (ncap - 1);
        slots[k] = (uint32_t)i + 1;
    }
    free(in->slots);
    in->slots = slots;
    in->cap = ncap;
}

/* id of s[0..n), inserting when `add`; (uint32_t)-1 when absent. */
static uint32_t in_id(Interner *in, const char *s, size_t n, bool add) {
    if (!in->cap) {
        if (!add) return (uint32_t)-1;
        in_grow(in);
    }
    size_t k = fnv(s, n) & (in->cap - 1);
    while (in->slots[k]) {
        const char *e = in->names[in->slots[k] - 1];
        if (strlen(e) == n && memcmp(e, s, n) == 0) return in->slots[k] - 1;
        k = (k + 1) & (in->cap - 1);
    }
    if (!add) return (uint32_t)-1;
    if ((in->n + 1) * 2 > in->cap) {
        in_grow(in);
        k = fnv(s, n) & (in->cap - 1);
        while (in->slots[k]) k = (k + 1) & (in->cap - 1);
    }
    in->names = na_realloc(in->names, (in->n + 1) * sizeof *in->names);
    in->names[in->n] = na_strndup(s, n);
    in->slots[k] = (uint32_t)in->n + 1;
    return (uint32_t)in->n++;
}

WsIndex *wsi_new(Store *st, LspHub *hub) {
    WsIndex *ix = na_alloc(sizeof *ix);
    ix->st = st;
    ix->hub = hub;
    return ix;
}

static void entry_free(Entry *e) {
    free(e->path);
    symbols_free(&e->syms);
    free(e->ids);
}

void wsi_free(WsIndex *ix) {
    if (!ix) return;
    if (ix->user && ix->user_free) ix->user_free(ix->user);
    free(ix->blob);
    for (size_t i = 0; i < ix->n; i++) entry_free(&ix->v[i]);
    free(ix->v);
    for (size_t i = 0; i < ix->in.n; i++) free(ix->in.names[i]);
    free(ix->in.names);
    free(ix->in.slots);
    free(ix);
}

static bool c_family(const char *p) {
    static const char *const ext[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"};
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (path_has_suffix_ci(p, ext[i])) return true;
    return false;
}

static bool indexable(const WsIndex *ix, const char *p) {
    return c_family(p) || (ix->hub && lsp_hub_configured(ix->hub, p));
}

static bool skip_dir(const char *d) {
    return d[0] == '.' || strcmp(d, "node_modules") == 0 || strcmp(d, "__pycache__") == 0 || strcmp(d, "build") == 0 ||
           strcmp(d, "target") == 0 || strcmp(d, "vendor") == 0 || strcmp(d, "dist") == 0;
}

static int walk(const WsIndex *ix, const char *dir, const char *rel, StrList *out) {
    StrList f, d;
    sl_init(&f);
    sl_init(&d);
    if (pf_list_dir(dir, &f, &d) != 0) {
        sl_free(&f);
        sl_free(&d);
        return 0; /* unreadable directories are skipped */
    }
    int rc = 0;
    for (size_t i = 0; i < f.n && rc == 0; i++) {
        if (!indexable(ix, f.v[i])) continue;
        if (out->n >= MAX_FILES) {
            rc = na_fail("index_limit_exceeded: more than %d source files", MAX_FILES);
            break;
        }
        Buf b;
        buf_init(&b);
        if (rel[0]) buf_printf(&b, "%s/", rel);
        buf_puts(&b, f.v[i]);
        sl_push(out, b.p);
        buf_free(&b);
    }
    for (size_t i = 0; i < d.n && rc == 0; i++) {
        if (skip_dir(d.v[i])) continue;
        Buf a, r;
        buf_init(&a);
        buf_init(&r);
        buf_printf(&a, "%s/%s", dir, d.v[i]);
        if (rel[0]) buf_printf(&r, "%s/", rel);
        buf_puts(&r, d.v[i]);
        rc = walk(ix, a.p, r.p, out);
        buf_free(&a);
        buf_free(&r);
    }
    sl_free(&f);
    sl_free(&d);
    return rc;
}

static bool id_start(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
}
static bool id_part(unsigned char c) { return id_start(c) || (c >= '0' && c <= '9'); }

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void lex_idents(WsIndex *ix, Entry *e, const char *t, size_t n) {
    size_t cap = 64;
    uint32_t *ids = na_alloc(cap * sizeof *ids);
    size_t k = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)t[i];
        if (c >= '0' && c <= '9') { /* numbers (0x1F, 1e5) are not identifiers */
            while (i < n && id_part((unsigned char)t[i])) i++;
        } else if (id_start(c)) {
            size_t s = i;
            while (i < n && id_part((unsigned char)t[i])) i++;
            if (k == cap) {
                cap *= 2;
                ids = na_realloc(ids, cap * sizeof *ids);
            }
            ids[k++] = in_id(&ix->in, t + s, i - s, true);
        } else {
            i++;
        }
    }
    qsort(ids, k, sizeof *ids, cmp_u32);
    size_t u = 0;
    for (size_t i = 0; i < k; i++)
        if (u == 0 || ids[u - 1] != ids[i]) ids[u++] = ids[i];
    e->ids = ids;
    e->nids = u;
}

static void extract(WsIndex *ix, Entry *e, const char *t, size_t n) {
    free(e->ids);
    e->ids = NULL;
    symbols_free(&e->syms);
    e->server = false;
    lex_idents(ix, e, t, n);
    if (ix->hub && lsp_hub_configured(ix->hub, e->path)) {
        Buf abs;
        buf_init(&abs);
        buf_printf(&abs, "%s/%s", store_root(ix->st), e->path);
        SymbolList l;
        if (lsp_hub_outline(ix->hub, abs.p, t, n, true, &l) == 0) {
            e->syms = l;
            e->server = true;
        }
        buf_free(&abs);
        if (e->server) return;
    }
    if (c_family(e->path)) {
        Snapshot *snap = snap_new(0, t, n);
        SymbolList l;
        if (c23_outline(snap, true, &l) == 0) e->syms = l;
        snap_free(snap);
    }
}

static int cmp_entry(const void *a, const void *b) { return strcmp(((const Entry *)a)->path, ((const Entry *)b)->path); }

/* ---- on-disk cache: .aed/index.cache ----
 * Symbols, identifier sets and the hybrid-search state survive between runs of the one-shot CLI. The
 * file starts with the format + program version; anything unexpected (truncated, other version, unsorted)
 * discards the whole cache and the index is simply rebuilt. Stale entries are harmless: every file is
 * still checked by size + mtime (and by content hash when it was read), exactly as in a running server. */
#define CACHE_MAGIC "nextagent-index-cache/1 " NA_VERSION

static char *cache_path(const WsIndex *ix) {
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/.aed/index.cache", store_root(ix->st));
    return buf_take(&b);
}

static void cache_reset(WsIndex *ix) {
    for (size_t i = 0; i < ix->n; i++) entry_free(&ix->v[i]);
    ix->n = 0;
    for (size_t i = 0; i < ix->in.n; i++) free(ix->in.names[i]);
    ix->in.n = 0;
    if (ix->in.slots) memset(ix->in.slots, 0, ix->in.cap * sizeof *ix->in.slots);
    free(ix->blob);
    ix->blob = NULL;
    ix->blob_n = 0;
}

static void cache_load(WsIndex *ix) {
    char *path = cache_path(ix);
    Buf f;
    buf_init(&f);
    if (pf_read_file(path, &f) != 0) {
        buf_free(&f);
        free(path);
        return;
    }
    free(path);
    BinR r = {(const unsigned char *)f.p, f.len, 0, false};
    char *magic = binr_str(&r);
    if (!magic || strcmp(magic, CACHE_MAGIC) != 0) r.bad = true;
    free(magic);
    uint32_t nn = r.bad ? 0 : binr_u32(&r);
    if (binr_room(&r, nn, 4))
        for (uint32_t i = 0; i < nn && !r.bad; i++) {
            char *s = binr_str(&r);
            if (!s || in_id(&ix->in, s, strlen(s), true) != i) r.bad = true;
            free(s);
        }
    uint32_t ne = r.bad ? 0 : binr_u32(&r);
    if (binr_room(&r, ne, 8))
        for (uint32_t i = 0; i < ne && !r.bad; i++) {
            if (ix->n == ix->cap) {
                ix->cap = ix->cap ? ix->cap * 2 : 64;
                ix->v = na_realloc(ix->v, ix->cap * sizeof *ix->v);
            }
            Entry *e = &ix->v[ix->n++];
            memset(e, 0, sizeof *e);
            e->path = binr_str(&r);
            e->hash = binr_u64(&r);
            e->len = (size_t)binr_u64(&r);
            e->server = binr_u32(&r) != 0;
            e->sig.size = binr_u64(&r);
            e->sig.mtime_ms = (int64_t)binr_u64(&r);
            e->sig_at = (int64_t)binr_u64(&r);
            e->has_sig = binr_u32(&r) != 0;
            uint32_t k = binr_u32(&r);
            if (binr_room(&r, k, 4)) {
                e->ids = na_alloc((k ? k : 1) * sizeof *e->ids);
                e->nids = k;
                for (uint32_t j = 0; j < k; j++)
                    if ((e->ids[j] = binr_u32(&r)) >= nn) r.bad = true;
            }
            k = binr_u32(&r);
            if (binr_room(&r, k, 8)) {
                e->syms.v = na_alloc((k ? k : 1) * sizeof *e->syms.v);
                e->syms.cap = k;
                for (uint32_t j = 0; j < k && !r.bad; j++) {
                    Symbol *y = &e->syms.v[e->syms.n++];
                    y->name = binr_str(&r);
                    y->kind = binr_str(&r);
                    y->scope = binr_str(&r);
                    y->start = (size_t)binr_u64(&r);
                    y->end = (size_t)binr_u64(&r);
                    y->sel = (size_t)binr_u64(&r);
                    y->line = (size_t)binr_u64(&r);
                    y->column = (size_t)binr_u64(&r);
                }
            }
            if (!e->path || (i > 0 && strcmp(ix->v[ix->n - 2].path, e->path) >= 0)) r.bad = true;
        }
    uint64_t hn = r.bad ? 0 : binr_u64(&r);
    if (!r.bad && binr_room(&r, (size_t)hn, 1) && hn) {
        ix->blob = na_alloc((size_t)hn);
        memcpy(ix->blob, r.p + r.i, (size_t)hn);
        ix->blob_n = (size_t)hn;
    }
    buf_free(&f);
    if (r.bad) {
        cache_reset(ix);
        return;
    }
    /* a file the language server would now outline was cached from the scanner: extract it again */
    size_t k = 0;
    for (size_t i = 0; i < ix->n; i++) {
        if (ix->hub && !ix->v[i].server && lsp_hub_configured(ix->hub, ix->v[i].path)) entry_free(&ix->v[i]);
        else ix->v[k++] = ix->v[i];
    }
    ix->n = k;
}

/* Writes .aed/index.cache. `hyb` is the search's own section; NULL keeps the one that was loaded. */
void wsi_cache_save(WsIndex *ix, const Buf *hyb) {
    Buf b;
    buf_init(&b);
    bin_str(&b, CACHE_MAGIC);
    bin_u32(&b, (uint32_t)ix->in.n);
    for (size_t i = 0; i < ix->in.n; i++) bin_str(&b, ix->in.names[i]);
    bin_u32(&b, (uint32_t)ix->n);
    for (size_t i = 0; i < ix->n; i++) {
        const Entry *e = &ix->v[i];
        bin_str(&b, e->path);
        bin_u64(&b, e->hash);
        bin_u64(&b, (uint64_t)e->len);
        bin_u32(&b, e->server ? 1 : 0);
        bin_u64(&b, (uint64_t)e->sig.size);
        bin_u64(&b, (uint64_t)e->sig.mtime_ms);
        bin_u64(&b, (uint64_t)e->sig_at);
        bin_u32(&b, e->has_sig ? 1 : 0);
        bin_u32(&b, (uint32_t)e->nids);
        for (size_t j = 0; j < e->nids; j++) bin_u32(&b, e->ids[j]);
        bin_u32(&b, (uint32_t)e->syms.n);
        for (size_t j = 0; j < e->syms.n; j++) {
            const Symbol *y = &e->syms.v[j];
            bin_str(&b, y->name);
            bin_str(&b, y->kind);
            bin_str(&b, y->scope);
            bin_u64(&b, (uint64_t)y->start);
            bin_u64(&b, (uint64_t)y->end);
            bin_u64(&b, (uint64_t)y->sel);
            bin_u64(&b, (uint64_t)y->line);
            bin_u64(&b, (uint64_t)y->column);
        }
    }
    const char *hp = hyb ? hyb->p : ix->blob;
    size_t hn = hyb ? hyb->len : ix->blob_n;
    bin_u64(&b, (uint64_t)hn);
    if (hn) buf_put(&b, hp, hn);
    char *path = cache_path(ix);
    Buf dir;
    buf_init(&dir);
    buf_printf(&dir, "%s/.aed", store_root(ix->st));
    if (pf_mkdirs(dir.p) == 0 && pf_write_atomic(path, b.p, b.len) == 0) {
        ix->dirty = false;
        if (hyb) {
            free(ix->blob);
            ix->blob = hn ? na_alloc(hn) : NULL;
            if (hn) memcpy(ix->blob, hp, hn);
            ix->blob_n = hn;
        }
    }
    buf_free(&dir);
    free(path);
    buf_free(&b);
}

bool wsi_dirty(const WsIndex *ix) { return ix->dirty; }

/* the loaded hybrid-search section, once: the caller owns the returned memory */
char *wsi_take_blob(WsIndex *ix, size_t *n) {
    char *p = ix->blob;
    *n = ix->blob_n;
    ix->blob = NULL;
    ix->blob_n = 0;
    return p;
}

int wsi_refresh(WsIndex *ix) {
    if (!ix->cache_tried) {
        ix->cache_tried = true;
        cache_load(ix);
    }
    ix->extracted = 0;
    ix->skipped = 0;
    StrList files;
    sl_init(&files);
    if (walk(ix, store_root(ix->st), "", &files) != 0) {
        sl_free(&files);
        return -1;
    }
    StrList virt;
    sl_init(&virt);
    if (store_snapshot_paths(ix->st, &virt) == 0)
        for (size_t i = 0; i < virt.n; i++)
            if (indexable(ix, virt.v[i]) && !sl_has(&files, virt.v[i])) sl_push(&files, virt.v[i]);
    sl_sort(&files);
    size_t sorted = ix->n; /* entries from earlier refreshes are sorted by path */
    int64_t now = pf_unix_ms();
    for (size_t i = 0; i < ix->n; i++) ix->v[i].seen = false;
    for (size_t f = 0; f < files.n; f++) {
        char *text;
        size_t len;
        Entry *e = bsearch(files.v[f], ix->v, sorted, sizeof *ix->v, cmp_key);
        /* unchanged files are recognised by size + mtime without being read (no watcher thread needed:
         * the check is a stat per file, so a long-running server sees outside edits on its next query) */
        PfStat sig;
        bool have = false;
        if (!sl_has(&virt, files.v[f])) {
            Buf full;
            buf_init(&full);
            buf_printf(&full, "%s/%s", store_root(ix->st), files.v[f]);
            have = pf_stat(full.p, &sig);
            buf_free(&full);
            if (have && e && e->has_sig && sig.size == e->sig.size && sig.mtime_ms == e->sig.mtime_ms &&
                e->sig_at - sig.mtime_ms > 2000) {
                e->seen = true;
                ix->skipped++;
                continue;
            }
        }
        if (store_snapshot_text(ix->st, files.v[f], &text, &len) != 0) continue; /* vanished: dropped below */
        if (!e) {
            if (ix->n == ix->cap) {
                ix->cap = ix->cap ? ix->cap * 2 : 64;
                ix->v = na_realloc(ix->v, ix->cap * sizeof *ix->v);
            }
            e = &ix->v[ix->n++];
            memset(e, 0, sizeof *e);
            e->path = na_strdup(files.v[f]);
            e->len = (size_t)-1;
        }
        e->seen = true;
        ix->dirty = true; /* read from disk: the cached signature is outdated */
        e->has_sig = have;
        if (have) {
            e->sig = sig;
            e->sig_at = now;
        }
        uint64_t h = fnv(text, len);
        if (h != e->hash || len != e->len) {
            if (utf8_valid(text, len)) {
                extract(ix, e, text, len);
                ix->extracted++;
            }
            e->hash = h;
            e->len = len;
        }
        free(text);
    }
    sl_free(&files);
    sl_free(&virt);
    size_t k = 0;
    for (size_t i = 0; i < ix->n; i++) {
        if (ix->v[i].seen) ix->v[k++] = ix->v[i];
        else {
            entry_free(&ix->v[i]);
            ix->dirty = true;
        }
    }
    ix->n = k;
    qsort(ix->v, ix->n, sizeof *ix->v, cmp_entry);
    return 0;
}

size_t wsi_extracted(const WsIndex *ix) { return ix->extracted; }
size_t wsi_skipped(const WsIndex *ix) { return ix->skipped; }
uint64_t wsi_hash(const WsIndex *ix, size_t i) { return ix->v[i].hash; }
void *wsi_user(const WsIndex *ix) { return ix->user; }
void wsi_set_user(WsIndex *ix, void *p, void (*free_fn)(void *)) {
    ix->user = p;
    ix->user_free = free_fn;
}
size_t wsi_nfiles(const WsIndex *ix) { return ix->n; }
const char *wsi_path(const WsIndex *ix, size_t i) { return ix->v[i].path; }
const SymbolList *wsi_symbols(const WsIndex *ix, size_t i) { return &ix->v[i].syms; }
bool wsi_from_server(const WsIndex *ix, size_t i) { return ix->v[i].server; }

bool wsi_has_ident(const WsIndex *ix, size_t i, const char *name) {
    uint32_t id = in_id((Interner *)&ix->in, name, strlen(name), false);
    if (id == (uint32_t)-1) return false;
    const Entry *e = &ix->v[i];
    return bsearch(&id, e->ids, e->nids, sizeof *e->ids, cmp_u32) != NULL;
}
