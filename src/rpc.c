#include "rpc.h"
#include "semantic.h"
#include "c23.h"
#include "platform.h"
#include "refactor.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LINE (32u * 1024u * 1024u)

static J *j_clone(const J *v) {
    char *d = json_dump(v);
    J *c = json_parse(d, strlen(d));
    free(d);
    return c ? c : j_null();
}

J *edit_result_json(const EditResult *res) {
    J *o = j_obj(), *paths = j_arr();
    for (size_t i = 0; i < res->paths.n; i++) j_push(paths, j_str(res->paths.v[i]));
    j_set(o, "rev", j_int(res->rev));
    j_set(o, "paths", paths);
    return o;
}

/* ------------------------------------------------------ param validation */
static int need_str(const J *p, const char *key, const char **out) {
    const J *v = j_get(p, key);
    if (!v) return na_fail("missing parameter: %s", key);
    if (!j_is_str(v)) return na_fail("invalid parameter: %s must be a string", key);
    *out = v->s;
    return 0;
}

static int opt_str(const J *p, const char *key, const char **out) {
    const J *v = j_get(p, key);
    *out = NULL;
    if (!v || v->k == J_NULL) return 0;
    if (!j_is_str(v)) return na_fail("invalid parameter: %s must be a string", key);
    *out = v->s;
    return 0;
}

static int opt_int(const J *p, const char *key, int64_t dflt, int64_t *out) {
    const J *v = j_get(p, key);
    *out = dflt;
    if (!v || v->k == J_NULL) return 0;
    if (!j_is_int(v)) return na_fail("invalid parameter: %s must be an integer", key);
    *out = v->i;
    return 0;
}

static int opt_bool(const J *p, const char *key, bool dflt, bool *out) {
    const J *v = j_get(p, key);
    *out = dflt;
    if (!v || v->k == J_NULL) return 0;
    if (v->k != J_BOOL) return na_fail("invalid parameter: %s must be a boolean", key);
    *out = v->b;
    return 0;
}

static int need_int(const J *p, const char *key, int64_t *out) {
    if (!j_get(p, key)) return na_fail("missing parameter: %s", key);
    return opt_int(p, key, 0, out);
}

/* ops -> per-path change sets */
typedef struct {
    char **names;
    ChangeSet *sets;
    size_t n;
} Batch;

static void batch_free(Batch *b) {
    for (size_t i = 0; i < b->n; i++) {
        free(b->names[i]);
        cs_free(&b->sets[i]);
    }
    free(b->names);
    free(b->sets);
    memset(b, 0, sizeof *b);
}

static int batch_from_ops(const J *ops, Batch *b) {
    memset(b, 0, sizeof *b);
    if (!ops) return na_fail("missing parameter: ops");
    if (ops->k != J_ARR) return na_fail("invalid parameter: ops must be an array");
    if (ops->n == 0) return na_fail("invalid parameter: ops must not be empty");
    b->names = na_alloc(ops->n * sizeof *b->names);
    b->sets = na_alloc(ops->n * sizeof *b->sets);
    for (size_t i = 0; i < ops->n; i++) {
        const J *op = ops->items[i];
        const char *path = NULL, *text = NULL;
        int64_t start = 0, end = 0;
        if (!op || op->k != J_OBJ) {
            batch_free(b);
            return na_fail("invalid parameter: ops[%zu] must be an object", i);
        }
        if (need_str(op, "path", &path) || need_int(op, "start", &start) || need_int(op, "end", &end) ||
            need_str(op, "text", &text)) {
            batch_free(b);
            return -1;
        }
        if (start < 0 || end < start) {
            batch_free(b);
            return na_fail("invalid parameter: ops[%zu] has an invalid range", i);
        }
        size_t k = 0;
        while (k < b->n && strcmp(b->names[k], path) != 0) k++;
        if (k == b->n) {
            b->names[b->n] = na_strdup(path);
            cs_init(&b->sets[b->n]);
            b->n++;
        }
        cs_add(&b->sets[k], (size_t)start, (size_t)end, text, strlen(text));
    }
    for (size_t i = 0; i < b->n; i++) {
        cs_sort(&b->sets[i]);
        if (cs_validate(&b->sets[i]) != 0) {
            batch_free(b);
            return -1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- methods */
static const Validator *validator_of(Rpc *r) { return r->has_validator ? &r->validator : NULL; }

static OutlineCache *outline_cache_of(Rpc *r, const char *target) {
    char *canon = store_canon(r->store, target);
    if (!canon) return NULL;
    for (size_t i = 0; i < r->ncaches; i++)
        if (strcmp(r->cache_paths[i], canon) == 0) {
            free(canon);
            return r->caches[i];
        }
    size_t slot = r->ncaches;
    if (slot == 32) {
        slot = r->next_evict++ % 32;
        free(r->cache_paths[slot]);
        outline_cache_free(r->caches[slot]);
    } else {
        r->ncaches++;
    }
    r->cache_paths[slot] = canon;
    r->caches[slot] = outline_cache_new();
    return r->caches[slot];
}

void rpc_release(Rpc *r) {
    for (size_t i = 0; i < r->ncaches; i++) {
        free(r->cache_paths[i]);
        outline_cache_free(r->caches[i]);
    }
    r->ncaches = 0;
    wsi_free(r->index);
    r->index = NULL;
}

static bool is_c_family(const char *p) {
    static const char *const exts[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"};
    for (size_t i = 0; i < sizeof exts / sizeof *exts; i++)
        if (path_has_suffix_ci(p, exts[i])) return true;
    return false;
}

/* Language servers are asked first; C-family files fall back to the built-in scanner when no server
 * is installed or the request fails. */
static bool use_lsp(Rpc *r, const char *target) { return r->hub && lsp_hub_handles(r->hub, target); }

static int lsp_symbols(Rpc *r, const char *target, const Snapshot *s, bool nested, SymbolList *out) {
    char *canon = store_canon(r->store, target);
    if (!canon) return -1;
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", store_root(r->store), canon);
    free(canon);
    char *abs = buf_take(&b);
    int rc = lsp_hub_outline(r->hub, abs, s->text, s->len, nested, out);
    free(abs);
    return rc;
}

static int outline_any(Rpc *r, const char *target, const Snapshot *s, bool nested, SymbolList *out) {
    if (use_lsp(r, target)) {
        if (lsp_symbols(r, target, s, nested, out) == 0) return 0;
        if (!is_c_family(target)) return -1;
    }
    OutlineCache *oc = outline_cache_of(r, target);
    return oc ? c23_outline_cached(oc, s, nested, out) : c23_outline(s, nested, out);
}

static int select_any(Rpc *r, const char *target, const Snapshot *s, const char *name, const char *kind,
                      const char *scope, Symbol *out) {
    if (use_lsp(r, target)) {
        SymbolList all;
        if (lsp_symbols(r, target, s, true, &all) == 0) {
            int rc = symbols_select(&all, name, kind, scope, out);
            symbols_free(&all);
            return rc;
        }
        if (!is_c_family(target)) return -1;
    }
    OutlineCache *oc = outline_cache_of(r, target);
    return oc ? c23_select_cached(oc, s, name, kind, scope, out) : c23_select(s, name, kind, scope, out);
}

static int replace_any(Rpc *r, const char *target, const Snapshot *s, const char *name, const char *replacement,
                       const char *kind, const char *scope, ChangeSet *cs) {
    if (use_lsp(r, target)) {
        Symbol sym;
        if (select_any(r, target, s, name, kind, scope, &sym) != 0) return -1;
        cs_init(cs);
        cs_add(cs, sym.start, sym.end, replacement, strlen(replacement));
        free(sym.name);
        free(sym.kind);
        free(sym.scope);
        return 0;
    }
    OutlineCache *oc = outline_cache_of(r, target);
    return oc ? c23_replace_symbol_cached(oc, s, name, replacement, strlen(replacement), kind, scope, cs)
              : c23_replace_symbol(s, name, replacement, strlen(replacement), kind, scope, cs);
}

static int m_view(Rpc *r, const J *p, J **out) {
    const char *target;
    if (need_str(p, "target", &target)) return -1;
    int64_t first, last;
    const char *symbol, *kind, *scope;
    if (opt_int(p, "start_line", 0, &first) || opt_int(p, "end_line", 0, &last) || opt_str(p, "symbol", &symbol) ||
        opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope))
        return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    J *o = j_obj();
    j_set(o, "rev", j_int(s->rev));
    j_set(o, "path", j_str(target));
    j_set(o, "lines", j_int((int64_t)s->nlines));
    if (symbol) { /* just the declaration/definition of one symbol */
        Symbol sym;
        if (select_any(r, target, s, symbol, kind, scope, &sym) != 0) {
            json_free(o);
            snap_free(s);
            return -1;
        }
        size_t bs = cp_to_byte(s->text, s->len, sym.start), be = cp_to_byte(s->text, s->len, sym.end);
        if (bs == (size_t)-1) bs = s->len;
        if (be == (size_t)-1) be = s->len;
        j_set(o, "start_line", j_int((int64_t)sym.line));
        j_set(o, "text", j_strn(s->text + bs, be - bs));
        free(sym.name);
        free(sym.kind);
        free(sym.scope);
        snap_free(s);
        *out = o;
        return 0;
    }
    if (first > 0 || last > 0) { /* 1-based inclusive line window */
        size_t a = first > 0 ? (size_t)first - 1 : 0, b = last > 0 ? (size_t)last : s->nlines;
        if (b > s->nlines) b = s->nlines;
        if (a > b) a = b;
        size_t cs = a < s->nlines ? s->line_starts[a] : s->ncp, ce = b < s->nlines ? s->line_starts[b] : s->ncp;
        size_t bs = cp_to_byte(s->text, s->len, cs), be = cp_to_byte(s->text, s->len, ce);
        if (bs == (size_t)-1) bs = s->len;
        if (be == (size_t)-1) be = s->len;
        j_set(o, "start_line", j_int((int64_t)a + 1));
        j_set(o, "text", j_strn(s->text + bs, be - bs));
    } else {
        j_set(o, "text", j_strn(s->text, s->len));
    }
    snap_free(s);
    *out = o;
    return 0;
}

/* Text typed with bare LF is converted to CRLF when the document uses CRLF (and has no bare LF),
 * so edits never introduce mixed line endings. Returns a malloc'd copy. */
static char *adapt_eol(const Snapshot *s, const char *in, size_t n, size_t *outlen) {
    bool crlf = false, lf = false;
    for (size_t i = 0; i < s->len; i++)
        if (s->text[i] == '\n') {
            if (i > 0 && s->text[i - 1] == '\r') crlf = true;
            else lf = true;
        }
    bool in_crlf = false;
    for (size_t i = 0; i + 1 < n; i++)
        if (in[i] == '\r' && in[i + 1] == '\n') in_crlf = true;
    Buf b;
    buf_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (crlf && !lf && !in_crlf && in[i] == '\n') buf_putc(&b, '\r');
        buf_putc(&b, in[i]);
    }
    *outlen = b.len;
    return buf_take(&b);
}

/* Replaces literal text; the match must be unique unless `all` is set (like a precise patch). */
static int m_replace_text(Rpc *r, const J *p, J **out) {
    const char *target, *old, *repl, *label;
    bool all;
    int64_t if_rev;
    if (need_str(p, "target", &target) || need_str(p, "old", &old) || need_str(p, "new", &repl) ||
        opt_bool(p, "all", false, &all) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev) || opt_str(p, "label", &label))
        return -1;
    if (old[0] == 0) return na_fail("invalid parameter: old must not be empty");
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    size_t olen, rlen;
    char *old_a = adapt_eol(s, old, strlen(old), &olen), *repl_a = adapt_eol(s, repl, strlen(repl), &rlen);
    old = old_a;
    repl = repl_a;
    if (if_rev == STORE_NO_REV) if_rev = store_head(r->store);
    ChangeSet cs;
    cs_init(&cs);
    size_t count = 0;
    for (size_t i = 0; i + olen <= s->len; i++) {
        if (memcmp(s->text + i, old, olen) != 0) continue;
        if (count == 0 || all) {
            size_t a = utf8_cp_count(s->text, i);
            cs_add(&cs, a, a + utf8_cp_count(old, olen), repl, rlen);
        }
        count++;
        i += olen - 1;
    }
    int rc = 0;
    if (count == 0) rc = na_fail("text_not_found: %s does not contain the given text", target);
    else if (count > 1 && !all) rc = na_fail("ambiguous_text: %zu matches in %s (add context or pass all)", count, target);
    EditResult res;
    const char *names[1] = {target};
    if (rc == 0)
        rc = store_edit(r->store, names, &cs, 1, if_rev, label ? label : "replace_text", r->session ? r->session : "jsonrpc",
                        validator_of(r), &res);
    cs_free(&cs);
    snap_free(s);
    free(old_a);
    free(repl_a);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "replacements", j_int((int64_t)(all ? count : 1)));
    edit_result_free(&res);
    return 0;
}

static int m_outline(Rpc *r, const J *p, J **out) {
    const char *target;
    bool nested;
    if (need_str(p, "target", &target) || opt_bool(p, "nested", false, &nested)) return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    SymbolList list;
    int rc = outline_any(r, target, s, nested, &list);
    if (rc == 0) {
        J *arr = j_arr();
        for (size_t i = 0; i < list.n; i++) j_push(arr, symbol_to_json(&list.v[i]));
        J *o = j_obj();
        j_set(o, "symbols", arr);
        *out = o;
        symbols_free(&list);
    }
    snap_free(s);
    return rc;
}

static int m_select(Rpc *r, const J *p, J **out) {
    const char *target, *name, *kind, *scope;
    if (need_str(p, "target", &target) || need_str(p, "name", &name) || opt_str(p, "kind", &kind) ||
        opt_str(p, "scope", &scope))
        return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    Symbol sym;
    int rc = select_any(r, target, s, name, kind, scope, &sym);
    if (rc == 0) {
        *out = symbol_to_json(&sym);
        free(sym.name);
        free(sym.kind);
        free(sym.scope);
    }
    snap_free(s);
    return rc;
}

static int m_replace_symbol(Rpc *r, const J *p, J **out) {
    const char *target, *name, *replacement, *kind, *scope, *label;
    int64_t expected;
    if (need_str(p, "target", &target) || need_str(p, "name", &name) || need_str(p, "replacement", &replacement) ||
        need_int(p, "if_rev", &expected) || opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) ||
        opt_str(p, "label", &label))
        return -1;
    if (expected != store_head(r->store))
        return na_fail("stale_rev: expected %lld, current %lld", (long long)expected,
                       (long long)store_head(r->store));
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    ChangeSet cs;
    size_t rlen;
    char *adapted = adapt_eol(s, replacement, strlen(replacement), &rlen);
    int rc = replace_any(r, target, s, name, adapted, kind, scope, &cs);
    free(adapted);
    snap_free(s);
    if (rc != 0) return -1;
    EditResult res;
    const char *names[1] = {target};
    rc = store_edit(r->store, names, &cs, 1, expected, label ? label : "replace_symbol", r->session ? r->session : "jsonrpc",
                    validator_of(r), &res);
    cs_free(&cs);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_edit(Rpc *r, const J *p, J **out) {
    const char *label;
    int64_t if_rev;
    if (opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev)) return -1;
    Batch b;
    if (batch_from_ops(j_get(p, "ops"), &b) != 0) return -1;
    EditResult res;
    int rc = store_edit(r->store, (const char *const *)b.names, b.sets, b.n, if_rev, label ? label : "edit",
                        r->session ? r->session : "jsonrpc", validator_of(r), &res);
    batch_free(&b);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_history(Rpc *r, const J *p, J **out) {
    const char *op;
    if (opt_str(p, "op", &op)) return -1;
    if (!op) op = "log";
    if (strcmp(op, "log") == 0) {
        int64_t n;
        if (opt_int(p, "n", 20, &n)) return -1;
        if (n < 0 || n > 10000) return na_fail("invalid parameter: n out of range");
        *out = store_history(r->store, (int)n);
        return *out ? 0 : -1;
    }
    EditResult res;
    int rc;
    if (strcmp(op, "undo") == 0) {
        int64_t rev;
        if (opt_int(p, "rev", 0, &rev)) return -1;
        if (rev < 0) return na_fail("invalid parameter: rev must be non-negative");
        rc = store_undo(r->store, rev, validator_of(r), &res);
    } else if (strcmp(op, "redo") == 0) {
        rc = store_redo(r->store, validator_of(r), &res);
    } else {
        return na_fail("unsupported history operation");
    }
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_sync(Rpc *r, const J *p, J **out) {
    const char *op;
    if (opt_str(p, "op", &op)) return -1;
    if (op && strcmp(op, "flush") != 0) return na_fail("unsupported sync operation");
    const J *paths = j_get(p, "paths");
    char **names = NULL;
    size_t n = 0;
    if (paths && paths->k != J_NULL) {
        if (paths->k != J_ARR) return na_fail("invalid parameter: paths must be an array");
        names = na_alloc((paths->n + 1) * sizeof *names);
        for (size_t i = 0; i < paths->n; i++) {
            if (!j_is_str(paths->items[i])) {
                free(names);
                return na_fail("invalid parameter: paths must contain strings");
            }
            names[n++] = paths->items[i]->s;
        }
    }
    StrList written;
    sl_init(&written);
    int rc = store_flush(r->store, (const char *const *)names, n, &written);
    free(names);
    if (rc != 0) {
        sl_free(&written);
        return -1;
    }
    J *arr = j_arr(), *o = j_obj();
    for (size_t i = 0; i < written.n; i++) j_push(arr, j_str(written.v[i]));
    sl_free(&written);
    j_set(o, "ok", j_bool(true));
    j_set(o, "written", arr);
    *out = o;
    return 0;
}

static int m_open(Rpc *r, const J *p, J **out) {
    (void)p;
    J *o = j_obj();
    j_set(o, "name", j_str("NextAgent"));
    j_set(o, "version", j_str(NA_VERSION));
    j_set(o, "rev", j_int(store_head(r->store)));
    *out = o;
    return 0;
}

/* ------------------------------------------------------------ refactorings */
static int opt_paths(const J *p, const char *key, StrList *out) {
    sl_init(out);
    const J *v = j_get(p, key);
    if (!v || v->k == J_NULL) return 0;
    if (v->k != J_ARR) return na_fail("invalid parameter: %s must be an array", key);
    for (size_t i = 0; i < v->n; i++) {
        if (!j_is_str(v->items[i])) {
            sl_free(out);
            return na_fail("invalid parameter: %s must contain strings", key);
        }
        sl_push(out, v->items[i]->s);
    }
    return 0;
}

static const char *session_of(Rpc *r) { return r->session ? r->session : "jsonrpc"; }

static bool no_server(void) { return strncmp(na_errmsg(), "no_language_server", 18) == 0; }

/* The semantic engine (workspace index + language servers). */
static Sem sem_of(Rpc *r) {
    if (!r->index) r->index = wsi_new(r->store, r->hub);
    Sem s = {r->store, r->hub, r->index};
    return s;
}

static int m_references(Rpc *r, const J *p, J **out) {
    const char *name, *kind, *scope, *path;
    bool members;
    StrList only;
    if (need_str(p, "name", &name) || opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) || opt_str(p, "path", &path) ||
        opt_bool(p, "members", true, &members) || opt_paths(p, "paths", &only))
        return -1;
    int rc = -1;
    if (r->hub) {
        Sem s = sem_of(r);
        rc = sem_references(&s, name, kind, scope, path ? path : (only.n == 1 ? only.v[0] : NULL), out);
        if (rc != 0 && no_server()) rc = -2;
    } else {
        rc = -2;
    }
    if (rc == -2) { /* no server for these files: token-based search for C */
        rc = rf_references(r->store, name, &only, members, out);
        if (rc == 0) j_set(*out, "source", j_str("syntax"));
    }
    sl_free(&only);
    return rc;
}

static int m_rename(Rpc *r, const J *p, J **out) {
    const char *name, *new_name, *label, *kind, *scope, *path;
    bool members, allow;
    int64_t if_rev;
    StrList only;
    if (need_str(p, "name", &name) || need_str(p, "new_name", &new_name) || opt_str(p, "label", &label) ||
        opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) || opt_str(p, "path", &path) ||
        opt_bool(p, "members", true, &members) || opt_bool(p, "allow_existing", false, &allow) ||
        opt_int(p, "if_rev", STORE_NO_REV, &if_rev) || opt_paths(p, "paths", &only))
        return -1;
    EditResult res;
    size_t sites = 0;
    int rc = -2;
    const char *source = "lsp";
    if (r->hub) {
        Sem s = sem_of(r);
        rc = sem_rename(&s, name, kind, scope, path ? path : (only.n == 1 ? only.v[0] : NULL), new_name, if_rev, label,
                        session_of(r), validator_of(r), &res, &sites);
        if (rc != 0 && !(no_server())) {
            sl_free(&only);
            return -1;
        }
    }
    if (rc != 0) { /* no language server for these files: token-based rename for C */
        RfCounts c;
        source = "syntax";
        rc = rf_rename(r->store, name, new_name, &only, members, allow, if_rev, label, session_of(r), validator_of(r), &res, &c);
        sites = c.sites;
    }
    sl_free(&only);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "sites", j_int((int64_t)sites));
    j_set(*out, "source", j_str(source));
    edit_result_free(&res);
    return 0;
}

static int m_add_param(Rpc *r, const J *p, J **out) {
    const char *function, *param, *def, *label, *kind, *scope, *path;
    int64_t pos, if_rev;
    StrList only;
    if (need_str(p, "function", &function) || need_str(p, "param", &param) || opt_str(p, "default", &def) ||
        opt_int(p, "position", -1, &pos) || opt_str(p, "label", &label) || opt_str(p, "kind", &kind) ||
        opt_str(p, "scope", &scope) || opt_str(p, "path", &path) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev) ||
        opt_paths(p, "paths", &only))
        return -1;
    EditResult res;
    RfCounts c = {0};
    const char *source = "lsp";
    int rc = -2;
    if (r->hub) { /* the server decides which occurrences are this function */
        Sem s = sem_of(r);
        rc = sem_add_param(&s, function, param, def, pos, kind, scope, path ? path : (only.n == 1 ? only.v[0] : NULL),
                           if_rev, label, session_of(r), validator_of(r), &res, &c);
        if (rc != 0 && !no_server()) {
            sl_free(&only);
            return -1;
        }
    }
    if (rc != 0) { /* no language server for these files: token-based for C */
        source = "syntax";
        rc = rf_add_param(r->store, function, param, def, pos, &only, if_rev, label, session_of(r), validator_of(r), &res, &c);
    }
    sl_free(&only);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "declarations", j_int((int64_t)c.sites));
    j_set(*out, "calls", j_int((int64_t)c.calls));
    j_set(*out, "source", j_str(source));
    edit_result_free(&res);
    return 0;
}


static int m_add_include(Rpc *r, const J *p, J **out) {
    const char *target, *header, *label;
    bool system;
    int64_t if_rev;
    if (need_str(p, "target", &target) || need_str(p, "header", &header) || opt_bool(p, "system", false, &system) ||
        opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev))
        return -1;
    EditResult res;
    bool changed;
    if (rf_add_include(r->store, target, header, system, if_rev, label, session_of(r), validator_of(r), &res, &changed) != 0)
        return -1;
    *out = edit_result_json(&res);
    j_set(*out, "changed", j_bool(changed));
    edit_result_free(&res);
    return 0;
}

static int m_search(Rpc *r, const J *p, J **out) {
    const char *query, *path;
    bool word, icase;
    int64_t context, limit;
    if (need_str(p, "query", &query) || opt_str(p, "path", &path) || opt_bool(p, "word", false, &word) ||
        opt_bool(p, "ignore_case", false, &icase) || opt_int(p, "context", 0, &context) || opt_int(p, "limit", 100, &limit))
        return -1;
    if (context < 0 || limit < 0) return na_fail("invalid parameter: context and limit must be non-negative");
    Sem s = sem_of(r);
    return sem_search(&s, query, word, icase, (size_t)context, path, (size_t)limit, out);
}

/* Several literal replacements in several files as one revision:
 * edits: [{path, old, new, all?}], each `old` must match exactly once unless `all`. */
static int m_patch(Rpc *r, const J *p, J **out) {
    const J *edits = j_get(p, "edits");
    const char *label;
    int64_t if_rev;
    if (opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev)) return -1;
    if (!edits || edits->k != J_ARR || edits->n == 0) return na_fail("invalid parameter: edits must be a non-empty array");
    if (if_rev == STORE_NO_REV) if_rev = store_head(r->store);
    char **names = na_alloc(edits->n * sizeof *names);
    Snapshot **snaps = na_alloc(edits->n * sizeof *snaps);
    ChangeSet *sets = na_alloc(edits->n * sizeof *sets);
    size_t n = 0;
    int rc = 0;
    size_t total = 0;
    for (size_t e = 0; e < edits->n && rc == 0; e++) {
        const J *ed = edits->items[e];
        const J *jp = j_get(ed, "path"), *jo = j_get(ed, "old"), *jn = j_get(ed, "new"), *ja = j_get(ed, "all");
        if (!j_is_str(jp) || !j_is_str(jo) || !j_is_str(jn) || jo->slen == 0) {
            rc = na_fail("invalid parameter: edits[%zu] needs path, old (non-empty) and new", e);
            break;
        }
        size_t slot = n;
        for (size_t k = 0; k < n; k++)
            if (strcmp(names[k], jp->s) == 0) slot = k;
        if (slot == n) {
            if (store_view(r->store, jp->s, &snaps[n]) != 0) {
                rc = -1;
                break;
            }
            names[n] = na_strdup(jp->s);
            cs_init(&sets[n]);
            n++;
        }
        const Snapshot *s = snaps[slot];
        size_t olen, rlen;
        char *old = adapt_eol(s, jo->s, jo->slen, &olen), *neu = adapt_eol(s, jn->s, jn->slen, &rlen);
        bool all = ja && ja->k == J_BOOL && ja->b;
        size_t count = 0;
        for (size_t i = 0; i + olen <= s->len; i++) {
            if (memcmp(s->text + i, old, olen) != 0) continue;
            if (count == 0 || all) {
                size_t a = utf8_cp_count(s->text, i);
                cs_add(&sets[slot], a, a + utf8_cp_count(old, olen), neu, rlen);
                total++;
            }
            count++;
            i += olen - 1;
        }
        if (count == 0) rc = na_fail("text_not_found: edits[%zu]: %s does not contain the given text", e, jp->s);
        else if (count > 1 && !all) rc = na_fail("ambiguous_text: edits[%zu]: %zu matches in %s (add context or set all)", e, count, jp->s);
        free(old);
        free(neu);
    }
    for (size_t k = 0; rc == 0 && k < n; k++) {
        cs_sort(&sets[k]);
        if (cs_validate(&sets[k]) != 0) rc = na_fail("overlapping_edits: %s", names[k]);
    }
    EditResult res;
    if (rc == 0)
        rc = store_edit(r->store, (const char *const *)names, sets, n, if_rev, label ? label : "patch",
                        r->session ? r->session : "jsonrpc", validator_of(r), &res);
    for (size_t k = 0; k < n; k++) {
        free(names[k]);
        cs_free(&sets[k]);
        snap_free(snaps[k]);
    }
    free(names);
    free(snaps);
    free(sets);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "replacements", j_int((int64_t)total));
    edit_result_free(&res);
    return 0;
}

static int m_find_symbols(Rpc *r, const J *p, J **out) {
    const char *query, *kind;
    int64_t limit;
    if (need_str(p, "query", &query) || opt_str(p, "kind", &kind) || opt_int(p, "limit", 100, &limit)) return -1;
    if (limit < 0) return na_fail("invalid parameter: limit must be non-negative");
    Sem s = sem_of(r);
    return sem_find_symbols(&s, query, kind, (size_t)limit, out);
}

typedef struct {
    const char *name;
    int (*fn)(Rpc *, const J *, J **);
} Method;

static int m_lsp_servers(Rpc *r, const J *p, J **out) {
    (void)p;
    J *o = j_obj();
    j_set(o, "servers", r->hub ? lsp_hub_status(r->hub) : j_arr());
    j_set(o, "enabled", j_bool(r->hub != NULL));
    *out = o;
    return 0;
}

static const Method METHODS[] = {
    {"open", m_open},       {"view", m_view},   {"outline", m_outline}, {"select", m_select},
    {"replace_symbol", m_replace_symbol},       {"replace_text", m_replace_text},       {"search", m_search}, {"patch", m_patch},       {"edit", m_edit},      {"history", m_history},
    {"sync", m_sync},       {"references", m_references}, {"rename", m_rename},
    {"add_param", m_add_param}, {"add_include", m_add_include}, {"find_symbols", m_find_symbols},
    {"lsp_servers", m_lsp_servers},
};

/* 0 ok, -1 failure (na_errmsg), -2 unknown method */
static int call_method(Rpc *r, const char *method, const J *params, J **out) {
    *out = NULL;
    for (size_t i = 0; i < sizeof METHODS / sizeof METHODS[0]; i++)
        if (strcmp(METHODS[i].name, method) == 0) return METHODS[i].fn(r, params, out);
    return -2;
}

/* ------------------------------------------------------------- JSON-RPC */
static J *make_error(const J *id, int code, const char *message) {
    J *o = j_obj(), *e = j_obj();
    j_set(o, "jsonrpc", j_str("2.0"));
    j_set(o, "id", id ? j_clone(id) : j_null());
    j_set(e, "code", j_int(code));
    j_set(e, "message", j_str(message));
    j_set(o, "error", e);
    return o;
}

static J *make_result(const J *id, J *result) {
    J *o = j_obj();
    j_set(o, "jsonrpc", j_str("2.0"));
    j_set(o, "id", j_clone(id));
    j_set(o, "result", result ? result : j_null());
    return o;
}

static bool valid_id(const J *id) { return id->k == J_NULL || id->k == J_INT || id->k == J_STR; }

J *rpc_dispatch(Rpc *r, const J *req) {
    if (!req || req->k != J_OBJ) return make_error(NULL, -32600, "Invalid Request");
    const J *id = j_get(req, "id"), *method = j_get(req, "method"), *jv = j_get(req, "jsonrpc");
    if (!jv || !j_is_str(jv) || strcmp(jv->s, "2.0") != 0 || !j_is_str(method) || (id && !valid_id(id)))
        return make_error(id && valid_id(id) ? id : NULL, -32600, "Invalid Request");
    const J *params = j_get(req, "params");
    J *empty = NULL;
    if (!params) params = empty = j_obj();
    J *reply = NULL;
    if (params->k != J_OBJ) {
        if (id) reply = make_error(id, -32602, "Invalid params");
        json_free(empty);
        return reply;
    }
    J *result = NULL;
    int rc = call_method(r, method->s, params, &result);
    if (rc == -2) {
        if (id) reply = make_error(id, -32601, "Method not found");
    } else if (rc != 0) {
        if (id) reply = make_error(id, -32602, na_errmsg());
    } else if (id) {
        reply = make_result(id, result);
        result = NULL;
    }
    json_free(result);
    json_free(empty);
    return reply;
}

/* ------------------------------------------------------------------ MCP */
static J *tool_def(const char *name, const char *desc, const char *schema_json) {
    J *t = j_obj();
    j_set(t, "name", j_str(name));
    j_set(t, "description", j_str(desc));
    J *schema = json_parse(schema_json, strlen(schema_json));
    j_set(t, "inputSchema", schema ? schema : j_obj());
    return t;
}

static J *tools_list(void) {
    J *arr = j_arr();
    j_push(arr, tool_def("view", "Read a document at the current revision.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"}},\"required\":[\"target\"]}"));
    j_push(arr, tool_def("outline", "List C23 top-level (or nested) symbols.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"nested\":{\"type\":\"boolean\"}},\"required\":[\"target\"]}"));
    j_push(arr, tool_def("select", "Locate one symbol by name, kind and scope.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"}},\"required\":[\"target\",\"name\"]}"));
    j_push(arr, tool_def("replace_symbol", "Atomically replace one symbol (requires if_rev).",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"replacement\":{\"type\":\"string\"},\"if_rev\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"}},\"required\":[\"target\",\"name\",\"replacement\",\"if_rev\"]}"));
    j_push(arr, tool_def("replace_text", "Replace literal text in a document (unique match unless all is set).",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"old\":{\"type\":\"string\"},\"new\":{\"type\":\"string\"},\"all\":{\"type\":\"boolean\"},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"target\",\"old\",\"new\"]}"));
    j_push(arr, tool_def("search", "Grep the workspace: one hit per line with optional context (query is literal text).",
                         "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"word\":{\"type\":\"boolean\"},\"ignore_case\":{\"type\":\"boolean\"},\"context\":{\"type\":\"integer\"},\"path\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"query\"]}"));
    j_push(arr, tool_def("patch", "Apply several literal replacements across files as one revision: edits [{path, old, new, all}].",
                         "{\"type\":\"object\",\"properties\":{\"edits\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"old\":{\"type\":\"string\"},\"new\":{\"type\":\"string\"},\"all\":{\"type\":\"boolean\"}},\"required\":[\"path\",\"old\",\"new\"]}},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"edits\"]}"));
    j_push(arr, tool_def("edit", "Apply codepoint-range edits across files in one atomic revision.",
                         "{\"type\":\"object\",\"properties\":{\"ops\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"start\":{\"type\":\"integer\"},\"end\":{\"type\":\"integer\"},\"text\":{\"type\":\"string\"}},\"required\":[\"path\",\"start\",\"end\",\"text\"]}},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"ops\"]}"));
    j_push(arr, tool_def("history", "Revision log, undo (optionally of a given rev) or redo.",
                         "{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\",\"enum\":[\"log\",\"undo\",\"redo\"]},\"n\":{\"type\":\"integer\"},\"rev\":{\"type\":\"integer\"}}}"));
    j_push(arr, tool_def("sync", "Flush committed revisions to disk.",
                         "{\"type\":\"object\",\"properties\":{\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}}}"));
    j_push(arr, tool_def("references", "Find all references to one symbol across the workspace (language server; token search for C without a server).",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"members\":{\"type\":\"boolean\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},\"required\":[\"name\"]}"));
    j_push(arr, tool_def("rename", "Rename one symbol across the workspace in one atomic revision (language server rename; token rename for C without a server).",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"new_name\":{\"type\":\"string\"},\"if_rev\":{\"type\":\"integer\"},\"members\":{\"type\":\"boolean\"},\"allow_existing\":{\"type\":\"boolean\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"label\":{\"type\":\"string\"}},\"required\":[\"name\",\"new_name\"]}"));
    j_push(arr, tool_def("add_param", "Add a parameter to a function and an argument to every call site (call sites resolved by the language server; token search for C without one).",
                         "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"param\":{\"type\":\"string\"},\"default\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"},\"if_rev\":{\"type\":\"integer\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"label\":{\"type\":\"string\"}},\"required\":[\"function\",\"param\"]}"));
    j_push(arr, tool_def("add_include", "Insert an #include after the last include (no-op if present).",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"header\":{\"type\":\"string\"},\"system\":{\"type\":\"boolean\"},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"target\",\"header\"]}"));
    j_push(arr, tool_def("lsp_servers", "List the configured language servers and whether they are installed/running.",
                         "{\"type\":\"object\",\"properties\":{}}"));
    j_push(arr, tool_def("find_symbols", "Search symbols of every indexed language across the workspace by name substring.",
                         "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"query\"]}"));
    return arr;
}

J *rpc_mcp_dispatch(Rpc *r, const J *req) {
    if (!req || req->k != J_OBJ) return make_error(NULL, -32600, "Invalid Request");
    const J *id = j_get(req, "id"), *method = j_get(req, "method");
    if (!j_is_str(method) || (id && !valid_id(id))) return make_error(NULL, -32600, "Invalid Request");
    if (!id) return NULL; /* notifications (notifications/initialized, ...) */
    const char *m = method->s;
    if (strcmp(m, "initialize") == 0) {
        J *res = j_obj(), *caps = j_obj(), *tools = j_obj(), *info = j_obj();
        j_set(caps, "tools", tools);
        j_set(info, "name", j_str("nextagent"));
        j_set(info, "version", j_str(NA_VERSION));
        const J *params = j_get(req, "params");
        const J *pv = params ? j_get(params, "protocolVersion") : NULL;
        j_set(res, "protocolVersion", j_str(j_is_str(pv) ? pv->s : "2024-11-05"));
        j_set(res, "capabilities", caps);
        j_set(res, "serverInfo", info);
        return make_result(id, res);
    }
    if (strcmp(m, "ping") == 0) return make_result(id, j_obj());
    if (strcmp(m, "tools/list") == 0) {
        J *res = j_obj();
        j_set(res, "tools", tools_list());
        return make_result(id, res);
    }
    if (strcmp(m, "tools/call") == 0) {
        const J *params = j_get(req, "params");
        const J *name = params ? j_get(params, "name") : NULL;
        const J *args = params ? j_get(params, "arguments") : NULL;
        if (!j_is_str(name) || (args && args->k != J_OBJ && args->k != J_NULL))
            return make_error(id, -32602, "Invalid params");
        J *empty = NULL;
        if (!args || args->k == J_NULL) args = empty = j_obj();
        J *result = NULL;
        int rc = (strcmp(name->s, "open") == 0) ? -2 : call_method(r, name->s, args, &result);
        if (rc == -2) {
            json_free(empty);
            return make_error(id, -32602, "Unknown tool");
        }
        Buf text;
        buf_init(&text);
        if (rc == 0) json_write(result, &text);
        else buf_puts(&text, na_errmsg());
        J *content = j_arr(), *item = j_obj(), *res = j_obj();
        j_set(item, "type", j_str("text"));
        j_set(item, "text", j_strn(text.p ? text.p : "", text.len));
        j_push(content, item);
        j_set(res, "content", content);
        j_set(res, "isError", j_bool(rc != 0));
        buf_free(&text);
        json_free(result);
        json_free(empty);
        return make_result(id, res);
    }
    return make_error(id, -32601, "Method not found");
}

/* ----------------------------------------------------------------- serve */
static int read_line(FILE *in, Buf *line) {
    line->len = 0;
    if (line->p) line->p[0] = 0;
    int c;
    bool any = false;
    while ((c = fgetc(in)) != EOF) {
        any = true;
        if (c == '\n') return 1;
        if (line->len >= MAX_LINE) return -1;
        buf_putc(line, (char)c);
    }
    return any ? 1 : 0;
}

int rpc_serve(Rpc *r, FILE *in, FILE *out, bool mcp) {
    Buf line;
    buf_init(&line);
    int rc;
    while ((rc = read_line(in, &line)) > 0) {
        size_t a = 0;
        while (a < line.len && (line.p[a] == ' ' || line.p[a] == '\t' || line.p[a] == '\r')) a++;
        if (a == line.len) continue;
        J *reply;
        J *req = json_parse(line.p, line.len);
        if (!req) reply = make_error(NULL, -32700, "Parse error");
        else reply = mcp ? rpc_mcp_dispatch(r, req) : rpc_dispatch(r, req);
        json_free(req);
        if (reply) {
            Buf o;
            buf_init(&o);
            json_write(reply, &o);
            buf_putc(&o, '\n');
            fwrite(o.p, 1, o.len, out);
            fflush(out);
            buf_free(&o);
            json_free(reply);
        }
    }
    buf_free(&line);
    return rc < 0 ? na_fail("request line too long") : 0;
}
