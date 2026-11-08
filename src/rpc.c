#include "rpc.h"
#include "semantic.h"
#include "c23.h"
#include "platform.h"
#include "refactor.h"
#include <ctype.h>
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

/* Replaces whole lines start_line..end_line (1-based, inclusive) of a document. The replacement keeps the
 * file's line endings and a missing final newline is added when the replaced lines had one; an empty
 * replacement deletes the lines. */
static int replace_lines(Rpc *r, const J *p, J **out) {
    const char *target, *repl, *label;
    int64_t a_line, b_line, if_rev;
    if (need_str(p, "target", &target) || need_str(p, "new", &repl) || opt_int(p, "start_line", 0, &a_line) ||
        opt_int(p, "end_line", 0, &b_line) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev) || opt_str(p, "label", &label))
        return -1;
    if (b_line == 0) b_line = a_line;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    size_t nl = 0;
    for (size_t i = 0; i < s->len; i++)
        if (s->text[i] == '\n') nl++;
    if (s->len > 0 && s->text[s->len - 1] != '\n') nl++;
    if (a_line < 1 || b_line < a_line || (size_t)b_line > nl) {
        snap_free(s);
        return na_fail("invalid parameter: lines %lld-%lld out of range (%s has %zu lines)", (long long)a_line, (long long)b_line, target, nl);
    }
    size_t from = 0, to, line = 1;
    for (; line < (size_t)a_line; line++) {
        while (s->text[from] != '\n') from++;
        from++;
    }
    to = from;
    for (; line <= (size_t)b_line; line++) {
        while (to < s->len && s->text[to] != '\n') to++;
        if (to < s->len) to++;
    }

    bool had_eol = to > from && s->text[to - 1] == '\n', crlf = to - from >= 2 && s->text[to - 2] == '\r';
    size_t rlen;
    char *rep = adapt_eol(s, repl, strlen(repl), &rlen);
    Buf nb;
    buf_init(&nb);
    buf_put(&nb, rep, rlen);
    if (rlen > 0 && had_eol && rep[rlen - 1] != '\n') buf_puts(&nb, crlf ? "\r\n" : "\n");
    free(rep);
    if (if_rev == STORE_NO_REV) if_rev = store_head(r->store);
    ChangeSet cs;
    cs_init(&cs);
    size_t cp_from = utf8_cp_count(s->text, from);
    cs_add(&cs, cp_from, cp_from + utf8_cp_count(s->text + from, to - from), nb.p ? nb.p : "", nb.len);
    EditResult res;
    const char *names[1] = {target};
    int rc = store_edit(r->store, names, &cs, 1, if_rev, label ? label : "replace_lines", r->session ? r->session : "jsonrpc",
                        validator_of(r), &res);
    cs_free(&cs);
    buf_free(&nb);
    snap_free(s);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "replacements", j_int(1));
    edit_result_free(&res);
    return 0;
}

/* Replaces literal text; the match must be unique unless `all` is set (like a precise patch).
 * With start_line (and optionally end_line) it replaces those whole lines instead and `old` is not needed. */
static int m_replace_text(Rpc *r, const J *p, J **out) {
    if (j_get(p, "start_line")) return replace_lines(r, p, out);
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

/* ---- call graph: callers through references (language server, or tokens for C), callees through the
 * identifiers of a function body matched against the workspace's function symbols ---- */
static bool callable_kind(const char *k) {
    return strcmp(k, "function") == 0 || strcmp(k, "method") == 0 || strcmp(k, "constructor") == 0 || strcmp(k, "procedure") == 0;
}

static size_t cg_file_index(const WsIndex *ix, const char *path) {
    for (size_t i = 0; i < wsi_nfiles(ix); i++)
        if (strcmp(wsi_path(ix, i), path) == 0) return i;
    return (size_t)-1;
}

/* innermost callable symbol of file fi that contains codepoint offset pos */
static const Symbol *cg_enclosing(const WsIndex *ix, size_t fi, size_t pos) {
    const SymbolList *l = wsi_symbols(ix, fi);
    const Symbol *best = NULL;
    for (size_t k = 0; k < l->n; k++) {
        const Symbol *y = &l->v[k];
        if (!callable_kind(y->kind) || pos < y->start || pos >= y->end) continue;
        if (!best || y->end - y->start < best->end - best->start) best = y;
    }
    return best;
}

static size_t cg_cp_to_byte(const char *t, size_t len, size_t cp) {
    size_t b = 0;
    while (cp > 0 && b < len) {
        b += utf8_seq_len((unsigned char)t[b]);
        cp--;
    }
    return b < len ? b : len;
}

/* codepoint offset of the declared name inside symbol y (its first whole-word occurrence), or (size_t)-1 */
static size_t cg_name_token(Rpc *r, const char *rel, const Symbol *y) {
    if (y->sel != (size_t)-1) return y->sel;
    char *text = NULL;
    size_t len, found = (size_t)-1, nl = strlen(y->name);
    if (store_snapshot_text(r->store, rel, &text, &len) != 0) {
        free(text);
        return found;
    }
    size_t a = cg_cp_to_byte(text, len, y->start), e = cg_cp_to_byte(text, len, y->end);
    for (size_t i = a; i + nl <= e; i++) {
        bool left = i == a || !(isalnum((unsigned char)text[i - 1]) || text[i - 1] == '_');
        bool right = i + nl == len || !(isalnum((unsigned char)text[i + nl]) || text[i + nl] == '_');
        if (left && right && memcmp(text + i, y->name, nl) == 0) {
            found = utf8_cp_count(text, i);
            break;
        }
    }
    free(text);
    return found;
}

static bool cg_seen(const J *nodes, const char *path, const char *name, int64_t line) {
    for (size_t i = 0; i < nodes->n; i++) {
        const J *n = nodes->items[i];
        if (j_get(n, "line")->i == line && strcmp(j_get(n, "path")->s, path) == 0 && strcmp(j_get(n, "name")->s, name) == 0) return true;
    }
    return false;
}

static J *cg_node(const char *path, const Symbol *y, int depth, const char *via) {
    J *o = j_obj();
    j_set(o, "name", j_str(y->name));
    j_set(o, "kind", j_str(y->kind));
    j_set(o, "path", j_str(path));
    j_set(o, "line", j_int((int64_t)y->line));
    j_set(o, "depth", j_int(depth));
    if (via) j_set(o, "via", j_str(via));
    return o;
}

/* Reconcile files changed outside NextAgent with the journal (three-way merge by lines). */
static int m_merge(Rpc *r, const J *p, J **out) {
    const char *strategy;
    bool dry;
    StrList only, all;
    if (opt_str(p, "strategy", &strategy) || opt_bool(p, "dry_run", false, &dry) || opt_paths(p, "paths", &only)) return -1;
    sl_init(&all);
    if (only.n == 0) {
        if (store_snapshot_paths(r->store, &all) != 0) {
            sl_free(&only);
            sl_free(&all);
            return -1;
        }
    } else {
        for (size_t i = 0; i < only.n; i++) sl_push(&all, only.v[i]);
    }
    J *results = j_arr(), *paths = j_arr();
    int64_t conflicts = 0;
    int rc = 0;
    for (size_t i = 0; i < all.n && rc == 0; i++) {
        J *rep;
        if (store_merge_external(r->store, all.v[i], strategy, dry, &rep) != 0) {
            rc = -1;
            break;
        }
        const char *st = j_get(rep, "status")->s;
        const J *pp = j_get(rep, "paths");
        if (pp && pp->n == 1) j_push(paths, j_str(pp->items[0]->s));
        if (strcmp(st, "conflict") == 0) conflicts++;
        if (strcmp(st, "unchanged") != 0 || only.n > 0) j_push(results, rep);
        else json_free(rep);
    }
    sl_free(&only);
    sl_free(&all);
    if (rc != 0) {
        json_free(results);
        json_free(paths);
        return -1;
    }
    J *o = j_obj();
    j_set(o, "conflicts", j_int(conflicts));
    j_set(o, "results", results);
    if (!dry && paths->n > 0) {
        j_set(o, "paths", paths);
        j_set(o, "rev", j_int(store_head(r->store)));
    } else {
        json_free(paths);
    }
    *out = o;
    return 0;
}

static int m_callers(Rpc *r, const J *p, J **out) {
    const char *name, *kind, *scope, *path;
    int64_t depth, limit;
    if (need_str(p, "name", &name) || opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) || opt_str(p, "path", &path) ||
        opt_int(p, "depth", 1, &depth) || opt_int(p, "limit", 100, &limit))
        return -1;
    if (depth < 1 || depth > 5 || limit < 1 || limit > 1000) return na_fail("invalid parameter: depth 1..5, limit 1..1000");
    Sem s = sem_of(r);
    if (wsi_refresh(s.ix) != 0) return -1;
    J *nodes = j_arr();
    /* frontier of (name, path) pairs to ask references for */
    typedef struct {
        char *name, *path, *scope, *kind;
    } Target;
    Target *cur = na_alloc(sizeof *cur), *next = NULL;
    size_t ncur = 1, nnext = 0, cnext = 0;
    cur[0] = (Target){na_strdup(name), path ? na_strdup(path) : NULL, scope ? na_strdup(scope) : NULL, kind ? na_strdup(kind) : NULL};
    bool truncated = false;
    int rc = 0;
    for (int d = 1; d <= depth && ncur > 0 && rc == 0; d++) {
        for (size_t t = 0; t < ncur && rc == 0; t++) {
            J *rp = j_obj();
            j_set(rp, "name", j_str(cur[t].name));
            if (cur[t].path) j_set(rp, "path", j_str(cur[t].path));
            if (cur[t].scope) j_set(rp, "scope", j_str(cur[t].scope));
            if (cur[t].kind) j_set(rp, "kind", j_str(cur[t].kind));
            J *refs = NULL;
            int rr = m_references(r, rp, &refs);
            json_free(rp);
            if (rr != 0) {
                if (d == 1) rc = -1; /* the requested symbol itself must resolve */
                continue;            /* deeper levels: skip callers we cannot resolve */
            }
            const J *arr = j_get(refs, "references");
            for (size_t i = 0; arr && i < arr->n; i++) {
                const J *ref = arr->items[i], *def = j_get(ref, "definition"), *call = j_get(ref, "call");
                if ((def && def->b) || (call && !call->b)) continue;
                const char *rel = j_get(ref, "path")->s;
                size_t fi = cg_file_index(s.ix, rel);
                if (fi == (size_t)-1) continue;
                const Symbol *y = cg_enclosing(s.ix, fi, (size_t)j_get(ref, "start")->i);
                if (!y || (strcmp(y->name, cur[t].name) == 0 && cg_name_token(r, rel, y) == (size_t)j_get(ref, "start")->i)) continue; /* the definition itself */
                int64_t line = (int64_t)y->line;
                if (cg_seen(nodes, rel, y->name, line)) continue;
                if (nodes->n >= (size_t)limit) {
                    truncated = true;
                    break;
                }
                J *n = cg_node(rel, y, d, cur[t].name);
                J *sites = j_arr();
                for (size_t k = 0; k < arr->n; k++) { /* every call site inside this caller */
                    const J *o = arr->items[k];
                    if (strcmp(j_get(o, "path")->s, rel) != 0) continue;
                    size_t st = (size_t)j_get(o, "start")->i;
                    if (st >= y->start && st < y->end && !(j_get(o, "definition") && j_get(o, "definition")->b))
                        j_push(sites, j_int(j_get(o, "line")->i));
                }
                j_set(n, "sites", sites);
                j_push(nodes, n);
                if (d < depth) {
                    if (nnext == cnext) {
                        cnext = cnext ? cnext * 2 : 16;
                        next = na_realloc(next, cnext * sizeof *next);
                    }
                    next[nnext++] = (Target){na_strdup(y->name), na_strdup(rel), y->scope && y->scope[0] ? na_strdup(y->scope) : NULL, NULL};
                }
            }
            json_free(refs);
        }
        for (size_t t = 0; t < ncur; t++) {
            free(cur[t].name);
            free(cur[t].path);
            free(cur[t].scope);
            free(cur[t].kind);
        }
        free(cur);
        cur = next;
        ncur = nnext;
        next = NULL;
        nnext = cnext = 0;
    }
    for (size_t t = 0; t < ncur; t++) {
        free(cur[t].name);
        free(cur[t].path);
        free(cur[t].scope);
        free(cur[t].kind);
    }
    free(cur);
    if (rc != 0) {
        json_free(nodes);
        return -1;
    }
    J *o = j_obj();
    j_set(o, "of", j_str(name));
    j_set(o, "count", j_int((int64_t)nodes->n));
    j_set(o, "truncated", j_bool(truncated));
    j_set(o, "nodes", nodes);
    *out = o;
    return 0;
}

typedef struct {
    const char *name;
    uint32_t file, sym;
} CgFn;

static int cmp_cgfn(const void *a, const void *b) {
    const CgFn *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    if (c) return c;
    return x->file != y->file ? (x->file < y->file ? -1 : 1) : (x->sym < y->sym ? -1 : x->sym > y->sym);
}

static int m_callees(Rpc *r, const J *p, J **out) {
    const char *name, *kind, *scope, *path;
    int64_t depth, limit;
    if (need_str(p, "name", &name) || opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) || opt_str(p, "path", &path) ||
        opt_int(p, "depth", 1, &depth) || opt_int(p, "limit", 100, &limit))
        return -1;
    if (depth < 1 || depth > 5 || limit < 1 || limit > 1000) return na_fail("invalid parameter: depth 1..5, limit 1..1000");
    Sem s = sem_of(r);
    if (wsi_refresh(s.ix) != 0) return -1;
    size_t nf = wsi_nfiles(s.ix), nfn = 0, cfn = 0;
    CgFn *fns = NULL;
    for (size_t f = 0; f < nf; f++) {
        const SymbolList *l = wsi_symbols(s.ix, f);
        for (size_t k = 0; k < l->n; k++) {
            if (!callable_kind(l->v[k].kind)) continue;
            if (nfn == cfn) {
                cfn = cfn ? cfn * 2 : 256;
                fns = na_realloc(fns, cfn * sizeof *fns);
            }
            fns[nfn++] = (CgFn){l->v[k].name, (uint32_t)f, (uint32_t)k};
        }
    }
    qsort(fns, nfn, sizeof *fns, cmp_cgfn);
    /* the root: exactly one function of that name (narrowed by path / kind / scope) */
    CgFn root = {0};
    size_t matches = 0;
    for (size_t i = 0; i < nfn; i++) {
        const Symbol *y = &wsi_symbols(s.ix, fns[i].file)->v[fns[i].sym];
        if (strcmp(y->name, name) != 0) continue;
        if (path && strcmp(wsi_path(s.ix, fns[i].file), path) != 0) continue;
        if (kind && strcmp(kind, y->kind) != 0) continue;
        if (scope && strcmp(scope, y->scope ? y->scope : "") != 0) continue;
        root = fns[i];
        matches++;
    }
    if (matches != 1) {
        free(fns);
        if (matches == 0) return na_fail("symbol_not_found: no function named %s in the index", name);
        return na_fail("ambiguous_symbol: %zu functions named %s (narrow with path, kind or scope)", matches, name);
    }
    J *nodes = j_arr();
    CgFn *cur = na_alloc(sizeof *cur), *next = NULL;
    size_t ncur = 1, nnext = 0, cnext = 0;
    cur[0] = root;
    bool truncated = false;
    for (int d = 1; d <= depth && ncur > 0; d++) {
        for (size_t t = 0; t < ncur; t++) {
            const char *rel = wsi_path(s.ix, cur[t].file);
            const Symbol *self = &wsi_symbols(s.ix, cur[t].file)->v[cur[t].sym];
            char *text = NULL;
            size_t len;
            if (store_snapshot_text(r->store, rel, &text, &len) != 0) {
                free(text);
                continue;
            }
            size_t a = cg_cp_to_byte(text, len, self->start), e = cg_cp_to_byte(text, len, self->end);
            for (size_t i = a; i < e;) {
                unsigned char c = (unsigned char)text[i];
                if (c == '/' && i + 1 < e && text[i + 1] == '/') {
                    while (i < e && text[i] != '\n') i++;
                } else if (c == '/' && i + 1 < e && text[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < e && !(text[i] == '*' && text[i + 1] == '/')) i++;
                    i += 2;
                } else if (c == '"' || c == '\'') {
                    i++;
                    while (i < e && text[i] != (char)c && text[i] != '\n') i += text[i] == '\\' ? 2 : 1;
                    i++;
                } else if (isalpha(c) || c == '_' || c >= 0x80) {
                    size_t b = i;
                    while (i < e && (isalnum((unsigned char)text[i]) || text[i] == '_' || (unsigned char)text[i] >= 0x80)) i++;
                    char word[128];
                    size_t wl = i - b < sizeof word - 1 ? i - b : sizeof word - 1;
                    memcpy(word, text + b, wl);
                    word[wl] = 0;
                    CgFn key = {word, 0, 0};
                    size_t lo = 0, hi = nfn;
                    while (lo < hi) {
                        size_t mid = (lo + hi) / 2;
                        if (strcmp(fns[mid].name, key.name) < 0) lo = mid + 1;
                        else hi = mid;
                    }
                    size_t same = 0;
                    for (size_t k = lo; k < nfn && strcmp(fns[k].name, word) == 0; k++) same++;
                    for (size_t k = lo; k < nfn && strcmp(fns[k].name, word) == 0; k++) {
                        const Symbol *y = &wsi_symbols(s.ix, fns[k].file)->v[fns[k].sym];
                        const char *drel = wsi_path(s.ix, fns[k].file);
                        if (fns[k].file == cur[t].file && fns[k].sym == cur[t].sym) continue; /* recursion is not an edge */
                        if (cg_seen(nodes, drel, y->name, (int64_t)y->line)) continue;
                        if (nodes->n >= (size_t)limit) {
                            truncated = true;
                            break;
                        }
                        J *n = cg_node(drel, y, d, self->name);
                        if (same > 1) j_set(n, "ambiguous", j_bool(true)); /* several functions share this name */
                        j_push(nodes, n);
                        if (d < depth) {
                            if (nnext == cnext) {
                                cnext = cnext ? cnext * 2 : 16;
                                next = na_realloc(next, cnext * sizeof *next);
                            }
                            next[nnext++] = fns[k];
                        }
                    }
                } else {
                    i++;
                }
            }
            free(text);
        }
        free(cur);
        cur = next;
        ncur = nnext;
        next = NULL;
        nnext = cnext = 0;
    }
    free(cur);
    free(fns);
    J *o = j_obj();
    j_set(o, "of", j_str(name));
    j_set(o, "count", j_int((int64_t)nodes->n));
    j_set(o, "truncated", j_bool(truncated));
    j_set(o, "nodes", nodes);
    *out = o;
    return 0;
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

/* Context pack: the best code for a question, as text, within a token budget (about 4 bytes per token).
 * Blocks are the ranked chunks of hybrid_search, overlapping ones are dropped, the last one is cut at a line. */
static int m_context(Rpc *r, const J *p, J **out) {
    const char *query, *path;
    int64_t budget, embed;
    if (need_str(p, "query", &query) || opt_str(p, "path", &path) || opt_int(p, "budget", 4000, &budget) ||
        opt_int(p, "embed_budget", 256, &embed))
        return -1;
    if (budget < 100 || budget > 200000) return na_fail("invalid parameter: budget must be 100..200000 tokens");
    Sem s = sem_of(r);
    J *res;
    if (sem_hybrid(&s, query, path, 40, (size_t)embed, &res) != 0) return -1;
    const J *m = j_get(res, "matches");
    J *blocks = j_arr();
    size_t cap = (size_t)budget * 4, used = 0;
    bool truncated = false;
    for (size_t i = 0; m && i < m->n && !truncated; i++) {
        const J *h = m->items[i];
        const char *rel = j_get(h, "path")->s;
        int64_t a = j_get(h, "start_line")->i, b = j_get(h, "end_line")->i;
        bool overlap = false;
        for (size_t k = 0; k < blocks->n && !overlap; k++) {
            const J *o = blocks->items[k];
            overlap = strcmp(j_get(o, "path")->s, rel) == 0 && a <= j_get(o, "end_line")->i && b >= j_get(o, "start_line")->i;
        }
        if (overlap) continue;
        char *text = NULL;
        size_t len;
        if (store_snapshot_text(r->store, rel, &text, &len) != 0) {
            free(text);
            continue;
        }
        size_t from = 0, to = 0, line = 1;
        while (line < (size_t)a && from < len) {
            if (text[from++] == '\n') line++;
        }
        to = from;
        int64_t last = a - 1;
        while (line <= (size_t)b && to < len) {
            size_t e = to;
            while (e < len && text[e] != '\n') e++;
            if (e < len) e++;
            if (used + (e - from) > cap) {
                truncated = true;
                break;
            }
            to = e;
            last = (int64_t)line;
            line++;
        }
        if (last >= a && to > from) {
            J *o = j_obj();
            j_set(o, "path", j_str(rel));
            j_set(o, "start_line", j_int(a));
            j_set(o, "end_line", j_int(last));
            const J *sym = j_get(h, "symbol");
            if (sym) j_set(o, "symbol", j_str(sym->s));
            j_set(o, "score", j_int(j_get(h, "score")->i));
            j_set(o, "text", j_strn(text + from, to - from));
            j_push(blocks, o);
            used += to - from;
        }
        free(text);
    }
    J *o = j_obj();
    j_set(o, "tokens", j_int((int64_t)(used / 4)));
    j_set(o, "budget", j_int(budget));
    j_set(o, "truncated", j_bool(truncated));
    j_set(o, "blocks", blocks);
    const J *em = j_get(res, "embedding");
    if (em && j_get(em, "error")) j_set(o, "embedding_error", j_str(j_get(em, "error")->s));
    json_free(res);
    *out = o;
    return 0;
}

static int m_hybrid_search(Rpc *r, const J *p, J **out) {
    const char *query, *path;
    int64_t limit, budget;
    if (need_str(p, "query", &query) || opt_str(p, "path", &path) || opt_int(p, "limit", 10, &limit) ||
        opt_int(p, "embed_budget", 256, &budget))
        return -1;
    if (limit < 0 || budget < 0) return na_fail("invalid parameter: limit and embed_budget must be non-negative");
    if (budget > 100000) budget = 100000;
    Sem s = sem_of(r);
    return sem_hybrid(&s, query, path, (size_t)limit, (size_t)budget, out);
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
    {"replace_symbol", m_replace_symbol},       {"replace_text", m_replace_text},       {"search", m_search}, {"hybrid_search", m_hybrid_search}, {"context", m_context}, {"merge", m_merge}, {"callers", m_callers}, {"callees", m_callees}, {"patch", m_patch},       {"edit", m_edit},      {"history", m_history},
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
    j_push(arr, tool_def("hybrid_search", "Ranked code search for a natural-language or identifier query: BM25 over code words and symbol/path names plus exact phrase, fused. Returns the best functions/chunks, not lines.",
                         "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"},\"embed_budget\":{\"type\":\"integer\"}},\"required\":[\"query\"]}"));
    j_push(arr, tool_def("context", "Context pack for a question: the most relevant functions/chunks as ready-to-read code, ranked by hybrid_search and cut to a token budget (default 4000). Use this first instead of several searches and views.",
                         "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"budget\":{\"type\":\"integer\"},\"path\":{\"type\":\"string\"}},\"required\":[\"query\"]}"));
    j_push(arr, tool_def("callers", "Who calls a function (transitively with depth): enclosing functions of its references, with call-site lines. Uses the language server when available, tokens for C otherwise.",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"depth\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"name\"]}"));
    j_push(arr, tool_def("callees", "What a function calls (transitively with depth): identifiers of its body matched to function symbols of the workspace index. Syntactic; names shared by several functions are flagged ambiguous.",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"depth\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"name\"]}"));
    j_push(arr, tool_def("merge", "Reconcile files that were changed outside NextAgent (sync reports disk_conflict): line-based three-way merge of baseline, journal and disk. strategy auto|ours|theirs; conflicts are reported, never written.",
                         "{\"type\":\"object\",\"properties\":{\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"strategy\":{\"type\":\"string\",\"enum\":[\"auto\",\"ours\",\"theirs\"]},\"dry_run\":{\"type\":\"boolean\"}}}"));
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
