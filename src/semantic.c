#include "semantic.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- texts and snapshots of the files a request touches ---- */
typedef struct {
    char *path; /* workspace-relative */
    char *text;
    size_t len;
    Snapshot *snap;
} Doc;

typedef struct {
    Sem *s;
    Doc *v;
    size_t n, cap;
} Docs;

static void docs_free(Docs *d) {
    for (size_t i = 0; i < d->n; i++) {
        free(d->v[i].path);
        free(d->v[i].text);
        snap_free(d->v[i].snap);
    }
    free(d->v);
    memset(d, 0, sizeof *d);
}

static Doc *docs_get(Docs *d, const char *rel) {
    for (size_t i = 0; i < d->n; i++)
        if (strcmp(d->v[i].path, rel) == 0) return &d->v[i];
    char *text;
    size_t len;
    if (store_snapshot_text(d->s->st, rel, &text, &len) != 0) return NULL;
    if (!utf8_valid(text, len)) {
        free(text);
        na_seterr("invalid_utf8: %s", rel);
        return NULL;
    }
    if (d->n == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 16;
        d->v = na_realloc(d->v, d->cap * sizeof *d->v);
    }
    Doc *x = &d->v[d->n++];
    x->path = na_strdup(rel);
    x->text = text;
    x->len = len;
    x->snap = NULL;
    return x;
}

static char *abs_of(const Sem *s, const char *rel) {
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", store_root(s->st), rel);
    return buf_take(&b);
}

/* absolute -> workspace-relative; NULL when outside the workspace */
static char *rel_of(const Sem *s, const char *abs) {
    const char *root = store_root(s->st);
    if (!path_inside(root, abs)) return NULL;
    const char *p = abs + strlen(root);
    while (*p == '/') p++;
    return na_strdup(p);
}

/* ---- locations -> codepoint ranges ---- */
struct Span {
    char *path;
    size_t start, end;
};
typedef struct Span Span;

static int cmp_span(const void *a, const void *b) {
    const Span *x = a, *y = b;
    int c = strcmp(x->path, y->path);
    if (c) return c;
    return x->start < y->start ? -1 : x->start > y->start;
}

static int to_spans(Sem *s, Docs *docs, const LspLocs *locs, Span **out, size_t *n) {
    Span *v = na_alloc((locs->n + 1) * sizeof *v);
    size_t k = 0;
    for (size_t i = 0; i < locs->n; i++) {
        const LspLoc *l = &locs->v[i];
        char *rel = rel_of(s, l->path);
        if (!rel) continue; /* outside the workspace (system headers, dependencies) */
        Doc *d = docs_get(docs, rel);
        if (!d) {
            free(rel);
            for (size_t j = 0; j < k; j++) free(v[j].path);
            free(v);
            return -1;
        }
        PosEnc enc = lsp_hub_encoding(s->hub, l->path);
        size_t a, b;
        if (lsp_position_to_offset(d->text, d->len, l->sl, l->sc, enc, &a) != 0 ||
            lsp_position_to_offset(d->text, d->len, l->el, l->ec, enc, &b) != 0 || b < a) {
            free(rel);
            continue;
        }
        v[k].path = rel;
        v[k].start = a;
        v[k].end = b;
        k++;
    }
    qsort(v, k, sizeof *v, cmp_span);
    size_t u = 0;
    for (size_t i = 0; i < k; i++) {
        if (u && strcmp(v[u - 1].path, v[i].path) == 0 && v[u - 1].start == v[i].start && v[u - 1].end == v[i].end) {
            free(v[i].path);
            continue;
        }
        v[u++] = v[i];
    }
    *out = v;
    *n = u;
    return 0;
}

static void spans_free_impl(Span *v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i].path);
    free(v);
}

/* ---- definition lookup ---- */
typedef struct {
    char *path;
    const Symbol *sym;
    size_t offset; /* codepoint offset of the name token */
} Def;

static bool whole_word_at(const char *t, size_t len, size_t at, size_t n) {
    bool before = at == 0 || !(isalnum((unsigned char)t[at - 1]) || t[at - 1] == '_' || (unsigned char)t[at - 1] >= 0x80);
    bool after = at + n >= len || !(isalnum((unsigned char)t[at + n]) || t[at + n] == '_' || (unsigned char)t[at + n] >= 0x80);
    return before && after;
}

static int name_offset(Docs *docs, const char *path, const Symbol *sym, const char *name, size_t *off) {
    if (sym->sel != (size_t)-1) {
        *off = sym->sel;
        return 0;
    }
    Doc *d = docs_get(docs, path);
    if (!d) return -1;
    size_t b0 = cp_to_byte(d->text, d->len, sym->start), b1 = cp_to_byte(d->text, d->len, sym->end), n = strlen(name);
    if (b0 == (size_t)-1) b0 = 0;
    if (b1 == (size_t)-1 || b1 > d->len) b1 = d->len;
    for (size_t i = b0; i + n <= b1; i++)
        if (memcmp(d->text + i, name, n) == 0 && whole_word_at(d->text, d->len, i, n)) {
            *off = utf8_cp_count(d->text, i);
            return 0;
        }
    return na_fail("symbol_not_found: cannot locate the name %s inside its declaration in %s", name, path);
}

static void spans_free(struct Span *v, size_t n);

#define MAX_DEFS 16

static int sync_candidates(Sem *s, Docs *docs, const char *name);
static int to_spans(Sem *s, Docs *docs, const LspLocs *locs, struct Span **out, size_t *n);

static int resolve(Sem *s, Docs *docs, const char *name, const char *kind, const char *scope, const char *path, Def *out) {
    if (wsi_refresh(s->ix) != 0) return -1;
    char *want = NULL;
    if (path && *path && !(want = store_canon(s->st, path))) return -1;
    Def defs[MAX_DEFS];
    size_t hits = 0, stored = 0;
    Buf cand;
    buf_init(&cand);
    for (size_t i = 0; i < wsi_nfiles(s->ix); i++) {
        if (want && strcmp(wsi_path(s->ix, i), want) != 0) continue;
        const SymbolList *l = wsi_symbols(s->ix, i);
        for (size_t k = 0; k < l->n; k++) {
            const Symbol *y = &l->v[k];
            if (strcmp(y->name, name) != 0) continue;
            if (kind && *kind && strcmp(kind, y->kind) != 0) continue;
            if (scope && strcmp(scope, y->scope) != 0) continue;
            if (hits < 8) buf_printf(&cand, "%s%s:%zu%s%s", hits ? ", " : "", wsi_path(s->ix, i), y->line, y->scope[0] ? " in " : "", y->scope);
            if (stored < MAX_DEFS) {
                defs[stored].path = na_strdup(wsi_path(s->ix, i));
                defs[stored].sym = y;
                defs[stored].offset = 0;
                stored++;
            }
            hits++;
        }
    }
    free(want);
    int rc = 0;
    if (hits == 0) rc = na_fail("symbol_not_found: %s", name);
    else if (hits > stored) rc = na_fail("ambiguous_symbol: %s (%zu definitions; narrow with kind, scope or path)", name, hits);
    for (size_t i = 0; rc == 0 && i < stored; i++) rc = name_offset(docs, defs[i].path, defs[i].sym, name, &defs[i].offset);
    /* A declaration and its definition are one entity: the server decides by listing both as references. */
    if (rc == 0 && stored > 1) {
        rc = sync_candidates(s, docs, name);
        LspLocs locs = {0};
        struct Span *spans = NULL;
        size_t ns = 0;
        if (rc == 0) {
            Doc *d = docs_get(docs, defs[0].path);
            char *abs = abs_of(s, defs[0].path);
            rc = d ? lsp_hub_references(s->hub, abs, d->text, d->len, defs[0].offset, true, &locs) : -1;
            free(abs);
        }
        if (rc == 0) rc = to_spans(s, docs, &locs, &spans, &ns);
        if (rc == 0) {
            size_t other = 0;
            for (size_t i = 1; i < stored; i++) {
                bool covered = false;
                for (size_t k = 0; k < ns && !covered; k++)
                    covered = strcmp(spans[k].path, defs[i].path) == 0 && spans[k].start == defs[i].offset;
                if (!covered) other++;
            }
            if (other) rc = na_fail("ambiguous_symbol: %s (%zu definitions: %s; narrow with kind, scope or path)", name, hits, cand.p);
        }
        spans_free(spans, ns);
        lsp_locs_free(&locs);
    }
    buf_free(&cand);
    if (rc == 0) {
        *out = defs[0];
        for (size_t i = 1; i < stored; i++) free(defs[i].path);
    } else {
        for (size_t i = 0; i < stored; i++) free(defs[i].path);
    }
    return rc;
}

/* Makes the servers see the current text of every file that mentions `name`. */
static int sync_candidates(Sem *s, Docs *docs, const char *name) {
    for (size_t i = 0; i < wsi_nfiles(s->ix); i++) {
        const char *p = wsi_path(s->ix, i);
        if (!wsi_has_ident(s->ix, i, name)) continue;
        char *abs = abs_of(s, p);
        bool handled = lsp_hub_handles(s->hub, abs);
        int rc = 0;
        if (handled) {
            Doc *d = docs_get(docs, p);
            rc = d ? lsp_hub_sync(s->hub, abs, d->text, d->len) : -1;
        }
        free(abs);
        if (rc != 0) return rc;
    }
    return 0;
}


static void spans_free(struct Span *v, size_t n) { spans_free_impl(v, n); }

/* ---- references ---- */
int sem_references(Sem *s, const char *name, const char *kind, const char *scope, const char *path, J **out) {
    if (!s->hub) return na_fail("no_language_server: no language server hub is enabled (start with --lsp)");
    Docs docs = {.s = s};
    Def def = {0};
    int rc = resolve(s, &docs, name, kind, scope, path, &def);
    if (rc == 0) rc = sync_candidates(s, &docs, name);
    LspLocs locs = {0};
    if (rc == 0) {
        Doc *d = docs_get(&docs, def.path);
        char *abs = abs_of(s, def.path);
        rc = d ? lsp_hub_references(s->hub, abs, d->text, d->len, def.offset, true, &locs) : -1;
        free(abs);
    }
    Span *spans = NULL;
    size_t ns = 0;
    if (rc == 0) rc = to_spans(s, &docs, &locs, &spans, &ns);
    if (rc == 0) {
        J *arr = j_arr();
        for (size_t i = 0; i < ns; i++) {
            Doc *d = docs_get(&docs, spans[i].path);
            if (!d->snap) d->snap = snap_new(0, d->text, d->len);
            size_t line = 0, col = 0;
            snap_position(d->snap, spans[i].start, &line, &col);
            J *o = j_obj();
            j_set(o, "path", j_str(spans[i].path));
            j_set(o, "start", j_int((int64_t)spans[i].start));
            j_set(o, "end", j_int((int64_t)spans[i].end));
            j_set(o, "line", j_int((int64_t)line));
            j_set(o, "column", j_int((int64_t)col));
            j_set(o, "definition", j_bool(strcmp(spans[i].path, def.path) == 0 && spans[i].start == def.offset));
            j_push(arr, o);
        }
        J *r = j_obj(), *dj = j_obj();
        j_set(dj, "path", j_str(def.path));
        j_set(dj, "kind", j_str(def.sym->kind));
        j_set(dj, "scope", j_str(def.sym->scope));
        j_set(dj, "line", j_int((int64_t)def.sym->line));
        j_set(r, "count", j_int((int64_t)arr->n));
        j_set(r, "definition", dj);
        j_set(r, "references", arr);
        j_set(r, "source", j_str("lsp"));
        *out = r;
    }
    spans_free(spans, ns);
    lsp_locs_free(&locs);
    free(def.path);
    docs_free(&docs);
    return rc;
}

/* ---- rename ---- */
typedef struct {
    char **names;
    ChangeSet *sets;
    size_t n, cap;
} Plan;

static ChangeSet *plan_for(Plan *p, const char *path) {
    for (size_t i = 0; i < p->n; i++)
        if (strcmp(p->names[i], path) == 0) return &p->sets[i];
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->names = na_realloc(p->names, p->cap * sizeof *p->names);
        p->sets = na_realloc(p->sets, p->cap * sizeof *p->sets);
    }
    p->names[p->n] = na_strdup(path);
    cs_init(&p->sets[p->n]);
    return &p->sets[p->n++];
}

static void plan_free(Plan *p) {
    for (size_t i = 0; i < p->n; i++) {
        free(p->names[i]);
        cs_free(&p->sets[i]);
    }
    free(p->names);
    free(p->sets);
    memset(p, 0, sizeof *p);
}

static bool valid_new_name(const char *n) {
    size_t len = strlen(n);
    if (len == 0 || len > 256) return false;
    for (size_t i = 0; i < len; i++)
        if (isspace((unsigned char)n[i]) || (unsigned char)n[i] < 0x20) return false;
    return true;
}

static bool unsupported(void) {
    const char *m = na_errmsg();
    return strstr(m, "rename_unsupported") || strstr(m, "Method not found") || strstr(m, "MethodNotFound");
}

int sem_rename(Sem *s, const char *name, const char *kind, const char *scope, const char *path,
               const char *new_name, int64_t if_rev, const char *label, const char *session,
               const Validator *validator, EditResult *res, size_t *sites) {
    if (!s->hub) return na_fail("no_language_server: no language server hub is enabled (start with --lsp)");
    if (!valid_new_name(new_name)) return na_fail("invalid parameter: new_name must be a single token without whitespace");
    if (strcmp(name, new_name) == 0) return na_fail("invalid parameter: new_name equals name");
    int64_t head = store_head(s->st);
    if (if_rev == STORE_NO_REV) if_rev = head;
    else if (if_rev != head) return na_fail("stale_rev: expected %lld, current %lld", (long long)if_rev, (long long)head);

    Docs docs = {.s = s};
    Def def = {0};
    Plan plan = {0};
    LspLocs locs = {0};
    int rc = resolve(s, &docs, name, kind, scope, path, &def);
    if (rc == 0) rc = sync_candidates(s, &docs, name);
    Doc *d = NULL;
    char *abs = NULL;
    if (rc == 0) {
        d = docs_get(&docs, def.path);
        abs = abs_of(s, def.path);
        rc = d ? lsp_hub_rename(s->hub, abs, d->text, d->len, def.offset, new_name, &locs) : -1;
        if (rc != 0 && d && unsupported()) { /* no rename support: replace every reference instead */
            rc = lsp_hub_references(s->hub, abs, d->text, d->len, def.offset, true, &locs);
            for (size_t i = 0; rc == 0 && i < locs.n; i++) locs.v[i].text = na_strdup(new_name);
        }
    }
    if (rc == 0) {
        size_t total = 0;
        for (size_t i = 0; i < locs.n && rc == 0; i++) {
            const LspLoc *l = &locs.v[i];
            char *rel = rel_of(s, l->path);
            if (!rel) {
                rc = na_fail("rename_outside_workspace: the server wants to edit %s", l->path);
                break;
            }
            Doc *dd = docs_get(&docs, rel);
            PosEnc enc = lsp_hub_encoding(s->hub, l->path);
            size_t a, b;
            if (!dd || lsp_position_to_offset(dd->text, dd->len, l->sl, l->sc, enc, &a) != 0 ||
                lsp_position_to_offset(dd->text, dd->len, l->el, l->ec, enc, &b) != 0 || b < a) {
                if (dd) na_seterr("invalid server edit range in %s", rel);
                rc = -1;
            } else {
                const char *t = l->text ? l->text : new_name;
                cs_add(plan_for(&plan, rel), a, b, t, strlen(t));
                total++;
            }
            free(rel);
        }
        if (rc == 0 && total == 0) rc = na_fail("symbol_not_found: the server reported no occurrences of %s", name);
        for (size_t i = 0; rc == 0 && i < plan.n; i++) {
            cs_sort(&plan.sets[i]);
            if (cs_validate(&plan.sets[i]) != 0) rc = na_fail("rename_conflict: overlapping edits in %s", plan.names[i]);
        }
        if (rc == 0 && sites) *sites = total;
    }
    if (rc == 0)
        rc = store_edit(s->st, (const char *const *)plan.names, plan.sets, plan.n, if_rev, label ? label : "rename",
                        session ? session : "jsonrpc", validator, res);
    free(abs);
    lsp_locs_free(&locs);
    plan_free(&plan);
    free(def.path);
    docs_free(&docs);
    return rc;
}

/* ---- find_symbols ---- */
static bool contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return true;
    for (const char *p = hay; *p; p++) {
        size_t k = 0;
        while (k < n && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return true;
    }
    return false;
}

int sem_find_symbols(Sem *s, const char *query, const char *kind, size_t limit, J **out) {
    if (limit == 0 || limit > 1000) limit = 100;
    if (wsi_refresh(s->ix) != 0) return -1;
    J *arr = j_arr();
    bool truncated = false;
    for (size_t i = 0; i < wsi_nfiles(s->ix) && !truncated; i++) {
        const SymbolList *l = wsi_symbols(s->ix, i);
        for (size_t k = 0; k < l->n; k++) {
            const Symbol *y = &l->v[k];
            if (kind && strcmp(kind, y->kind) != 0) continue;
            if (!contains_ci(y->name, query)) continue;
            if (arr->n >= limit) {
                truncated = true;
                break;
            }
            J *o = symbol_to_json(y);
            j_set(o, "path", j_str(wsi_path(s->ix, i)));
            j_push(arr, o);
        }
    }
    J *o = j_obj();
    j_set(o, "count", j_int((int64_t)arr->n));
    j_set(o, "truncated", j_bool(truncated));
    j_set(o, "symbols", arr);
    *out = o;
    return 0;
}
