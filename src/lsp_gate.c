#include "lsp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMP_ENTRIES 20000
#define MAX_OVERLAY 64

struct DiagnosticGate {
    char *root;
    LspOps ops;
    Store *store;
    int max_severity;
    StrList changed; /* paths read by the last validate() */
    uint8_t state[16];
    bool has_state;
};

static bool is_cxx_file(const char *p) {
    return path_has_suffix_ci(p, ".c") || path_has_suffix_ci(p, ".h") || path_has_suffix_ci(p, ".cc") ||
           path_has_suffix_ci(p, ".cpp") || path_has_suffix_ci(p, ".cxx") || path_has_suffix_ci(p, ".hh") ||
           path_has_suffix_ci(p, ".hpp") || path_has_suffix_ci(p, ".hxx");
}

static bool is_header(const char *p) {
    return path_has_suffix_ci(p, ".h") || path_has_suffix_ci(p, ".hh") || path_has_suffix_ci(p, ".hpp") ||
           path_has_suffix_ci(p, ".hxx");
}

DiagnosticGate *gate_new(const char *root, LspOps ops, Store *store, int maximum_severity) {
    DiagnosticGate *g = na_alloc(sizeof *g);
    g->root = path_normalize(root);
    g->ops = ops;
    g->store = store;
    g->max_severity = maximum_severity;
    sl_init(&g->changed);
    return g;
}

void gate_free(DiagnosticGate *g) {
    if (!g) return;
    free(g->root);
    sl_free(&g->changed);
    free(g);
}

static int store_text_cb(void *ctx, const char *name, char **text, size_t *len) {
    return store_snapshot_text((Store *)ctx, name, text, len);
}

static char *abs_of(const DiagnosticGate *g, const char *rel) {
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", g->root, rel);
    return buf_take(&b);
}

/* Files the gate validates: the C family always, anything else when the language server hub serves it. */
static bool gated(const DiagnosticGate *g, const char *rel) {
    if (is_cxx_file(rel)) return true;
    if (!g->ops.supports) return false;
    char *abs = abs_of(g, rel);
    bool ok = g->ops.supports(g->ops.ctx, abs);
    free(abs);
    return ok;
}

/* Discover dependents of the changed files. Returns dependents (workspace
 * relative) and loads the compilation database for include directories. */
static int discover(DiagnosticGate *g, const StrList *changed, StrList *dependents, CompDb *db,
                    StrList *snap_paths) {
    memset(db, 0, sizeof *db);
    if (comp_load(g->root, MAX_COMP_ENTRIES, db) != 0) return -1;
    if (store_snapshot_paths(g->store, snap_paths) != 0) return -1;
    DepOptions opt;
    memset(&opt, 0, sizeof opt);
    opt.max_files = 4096;
    opt.text = store_text_cb;
    opt.text_ctx = g->store;
    opt.snapshot_paths = snap_paths;
    opt.by_source = db->n ? db : NULL;
    /* Include-closure discovery only makes sense for C-family changes. */
    StrList cx;
    sl_init(&cx);
    for (size_t i = 0; i < changed->n; i++)
        if (is_cxx_file(changed->v[i])) sl_push(&cx, changed->v[i]);
    int rc = cx.n ? dependent_sources(g->root, &cx, &opt, dependents) : 0;
    sl_free(&cx);
    return rc;
}

/* Hash everything validation depended on besides the changed documents. */
static int compute_state(DiagnosticGate *g, uint8_t out[16]) {
    CompDb db;
    StrList deps, snaps;
    sl_init(&deps);
    sl_init(&snaps);
    Buf acc;
    buf_init(&acc);
    int rc = -1;
    if (discover(g, &g->changed, &deps, &db, &snaps) != 0) goto done;
    static const char *dbs[] = {"compile_commands.json", "build/compile_commands.json"};
    for (int i = 0; i < 2; i++) {
        char *p = abs_of(g, dbs[i]);
        Buf data;
        buf_init(&data);
        int r = pf_read_file(p, &data);
        free(p);
        if (r < 0) {
            buf_free(&data);
            goto done;
        }
        buf_printf(&acc, "db:%d:%zu:", i, data.len);
        if (data.len) buf_put(&acc, data.p, data.len);
        buf_free(&data);
    }
    for (size_t i = 0; i < deps.n; i++) buf_printf(&acc, "dep:%s;", deps.v[i]);
    for (size_t i = 0; i < snaps.n; i++) {
        if (sl_has(&g->changed, snaps.v[i])) continue;
        char *t = NULL;
        size_t tl = 0;
        if (store_snapshot_text(g->store, snaps.v[i], &t, &tl) != 0) goto done;
        buf_printf(&acc, "snap:%s:%zu:", snaps.v[i], tl);
        buf_put(&acc, t, tl);
        free(t);
    }
    blake2b16(acc.p ? acc.p : "", acc.len, out);
    rc = 0;
done:
    comp_free(&db);
    sl_free(&deps);
    sl_free(&snaps);
    buf_free(&acc);
    return rc;
}

typedef struct {
    char *rel;
    char *abs;
    char *uri;
    char *before;
    size_t blen;
    char *after; /* NULL = re-send before text (dependent) */
    size_t alen;
    bool report; /* compare diagnostics for this document */
    bool opened;
} Target;

static void target_free(Target *t) {
    free(t->rel);
    free(t->abs);
    free(t->uri);
    free(t->before);
    free(t->after);
}

static int gather(DiagnosticGate *g, const ValidateDoc *docs, size_t n, Target **out, size_t *nout) {
    StrList changed, deps, snaps;
    sl_init(&changed);
    sl_init(&deps);
    sl_init(&snaps);
    CompDb db;
    memset(&db, 0, sizeof db);
    Target *v = NULL;
    size_t cnt = 0, cap = 0;
    int rc = -1;
    for (size_t i = 0; i < n; i++)
        if (gated(g, docs[i].path)) sl_push(&changed, docs[i].path);
    if (changed.n == 0) {
        *out = NULL;
        *nout = 0;
        rc = 0;
        goto done;
    }
    if (discover(g, &changed, &deps, &db, &snaps) != 0) goto done;
    sl_free(&g->changed);
    g->changed = changed;
    sl_init(&changed);
    cap = n + deps.n + MAX_OVERLAY + 4;
    v = na_alloc(cap * sizeof *v);
    for (size_t i = 0; i < n; i++) {
        if (!gated(g, docs[i].path)) continue;
        Target *t = &v[cnt++];
        t->rel = na_strdup(docs[i].path);
        t->abs = abs_of(g, docs[i].path);
        t->blen = docs[i].before->len;
        t->before = na_strndup(docs[i].before->text, t->blen);
        t->alen = docs[i].after->len;
        t->after = na_strndup(docs[i].after->text, t->alen);
        t->report = true;
    }
    for (size_t i = 0; i < deps.n; i++) {
        Target *t = &v[cnt++];
        t->rel = na_strdup(deps.v[i]);
        t->abs = abs_of(g, deps.v[i]);
        if (store_snapshot_text(g->store, deps.v[i], &t->before, &t->blen) != 0) goto done_targets;
        t->report = true;
    }
    size_t overlay = 0;
    for (size_t i = 0; i < snaps.n; i++) {
        if (!is_header(snaps.v[i]) || sl_has(&g->changed, snaps.v[i])) continue;
        if (overlay++ >= MAX_OVERLAY) {
            na_seterr("too many unflushed headers to overlay");
            goto done_targets;
        }
        Target *t = &v[cnt++];
        t->rel = na_strdup(snaps.v[i]);
        t->abs = abs_of(g, snaps.v[i]);
        if (store_snapshot_text(g->store, snaps.v[i], &t->before, &t->blen) != 0) goto done_targets;
        t->report = false;
    }
    *out = v;
    *nout = cnt;
    rc = 0;
    goto done;
done_targets:
    for (size_t i = 0; i < cnt; i++) target_free(&v[i]);
    free(v);
done:
    comp_free(&db);
    sl_free(&changed);
    sl_free(&deps);
    sl_free(&snaps);
    return rc;
}

static void close_all(DiagnosticGate *g, Target *t, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (t[i].opened) g->ops.close_document(g->ops.ctx, t[i].uri);
}

static int validate_cb(void *ctx, const ValidateDoc *docs, size_t n) {
    DiagnosticGate *g = ctx;
    Target *t = NULL;
    size_t nt = 0;
    J **before = NULL;
    int rc = -1;
    g->has_state = false;
    if (gather(g, docs, n, &t, &nt) != 0) return -1;
    if (nt == 0) return 0;
    before = na_alloc(nt * sizeof *before);
    for (size_t i = 0; i < nt; i++) {
        if (g->ops.open_document(g->ops.ctx, t[i].abs, t[i].before, t[i].blen, 1, &t[i].uri) != 0) goto done;
        t[i].opened = true;
    }
    for (size_t i = 0; i < nt; i++) {
        J *report = NULL;
        if (g->ops.wait_diagnostics(g->ops.ctx, t[i].uri, 1, &report) != 0) goto done;
        before[i] = report;
    }
    /* Headers first: a source file's preamble is built from the headers as they are when its own change
     * arrives. */
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < nt; i++) {
            if (!t[i].after || is_header(t[i].rel) != (pass == 0)) continue;
            if (g->ops.change_document(g->ops.ctx, t[i].uri, t[i].after, t[i].alen, 2) != 0) goto done;
        }
    for (size_t i = 0; i < nt; i++) {
        if (t[i].after || !t[i].report) continue;
        /* clangd keeps a stale preamble for an unchanged dependent when only an
         * included (overlay) header changed; a fresh didOpen rebuilds it. */
        t[i].opened = false;
        char *old = t[i].uri;
        t[i].uri = NULL;
        int crc = g->ops.close_document(g->ops.ctx, old);
        free(old);
        if (crc != 0) goto done;
        if (g->ops.open_document(g->ops.ctx, t[i].abs, t[i].before, t[i].blen, 2, &t[i].uri) != 0) goto done;
        t[i].opened = true;
    }
    for (size_t i = 0; i < nt; i++) {
        J *report = NULL;
        if (g->ops.wait_diagnostics(g->ops.ctx, t[i].uri, 2, &report) != 0) goto done;
        if (!t[i].report) {
            json_free(report);
            continue;
        }
        const J *bd = NULL, *ad = NULL;
        DiagDelta delta;
        if (diag_require_fresh(before[i], 1, &bd) != 0 || diag_require_fresh(report, 2, &ad) != 0) {
            json_free(report);
            goto done;
        }
        if (diag_compare(bd, ad, g->max_severity, &delta) != 0) {
            json_free(report);
            goto done;
        }
        json_free(report);
        if (!delta.acceptable) {
            const DiagFingerprint *first = NULL;
            for (size_t k = 0; k < delta.nadded; k++)
                if (delta.added[k].severity <= g->max_severity) {
                    first = &delta.added[k];
                    break;
                }
            na_seterr("diagnostics_regression: %s: severity %d: %s", t[i].rel, first ? first->severity : 0,
                      first ? first->message : "");
            diag_delta_free(&delta);
            goto done;
        }
        diag_delta_free(&delta);
    }
    if (compute_state(g, g->state) != 0) goto done;
    g->has_state = true;
    rc = 0;
done:
    close_all(g, t, nt);
    for (size_t i = 0; i < nt; i++) {
        json_free(before[i]);
        target_free(&t[i]);
    }
    free(before);
    free(t);
    return rc;
}

static int recheck_cb(void *ctx) {
    DiagnosticGate *g = ctx;
    if (!g->has_state) return 0;
    uint8_t now[16];
    if (compute_state(g, now) != 0) return -1;
    if (memcmp(now, g->state, 16) != 0) return na_fail("stale_validation: build state changed during validation");
    return 0;
}

Validator gate_validator(DiagnosticGate *g) {
    Validator v;
    v.validate = validate_cb;
    v.recheck = recheck_cb;
    v.ctx = g;
    return v;
}
