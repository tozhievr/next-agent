#include "symbols.hpp"
#include "language.hpp"
#include <cctype>
#include <cstring>
#include <memory>
#include <string>

void symbols_free(SymbolList *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].name);
        free(l->v[i].kind);
        free(l->v[i].scope);
    }
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

J *symbol_to_json(const Symbol *s) {
    J *o = j_obj();
    j_set(o, "name", j_str(s->name));
    j_set(o, "kind", j_str(s->kind));
    j_set(o, "start", j_int((int64_t)s->start));
    j_set(o, "end", j_int((int64_t)s->end));
    j_set(o, "line", j_int((int64_t)s->line));
    j_set(o, "column", j_int((int64_t)s->column));
    j_set(o, "scope", j_str(s->scope));
    return o;
}

static bool eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int symbol_name_rank(const char *name, const char *query) {
    if (!name)
        name = "";
    if (!query)
        query = "";
    if (strcmp(name, query) == 0)
        return 0;
    if (eq_ci(name, query))
        return 1;
    if (!query[0])
        return 3;
    const char *h = name, *n = query;
    while (*n && *h && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
        h++;
        n++;
    }
    if (!*n)
        return 2;
    size_t qn = strlen(query);
    for (const char *p = name; *p; p++) {
        size_t k = 0;
        while (k < qn && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)query[k]))
            k++;
        if (k == qn)
            return 3;
    }
    return -1;
}

static void describe_symbol(Buf *cand, size_t n, const Symbol *s) {
    buf_printf(cand, "%s%s%s%s@%zu", n ? ", " : "", s->scope[0] ? s->scope : "", s->scope[0] ? "::" : "",
               s->kind, s->line);
}

static int copy_symbol(const Symbol *s, Symbol *out) {
    *out = *s;
    out->name = na_strdup(s->name);
    out->kind = na_strdup(s->kind);
    out->scope = na_strdup(s->scope);
    return 0;
}

static bool hint_ok(const Symbol *s, const char *kind, const char *scope) {
    if (kind && strcmp(s->kind, kind) != 0)
        return false;
    if (scope && strcmp(s->scope, scope) != 0)
        return false;
    return true;
}

int symbols_select(const SymbolList *all, const char *name, const char *kind, const char *scope,
                   Symbol *out) {
    long filtered_at = -1, exact_at = -1, ci_at = -1;
    size_t filtered = 0, exact = 0, ci = 0;
    Buf cand;
    buf_init(&cand);
    for (size_t i = 0; i < all->n; i++) {
        const Symbol *s = &all->v[i];
        if (strcmp(s->name, name) != 0)
            continue;
        if (exact == 0)
            exact_at = (long)i;
        if (exact < 6)
            describe_symbol(&cand, exact, s);
        exact++;
        if (!hint_ok(s, kind, scope))
            continue;
        if (filtered == 0)
            filtered_at = (long)i;
        filtered++;
    }
    if (filtered == 1) {
        buf_free(&cand);
        return copy_symbol(&all->v[filtered_at], out);
    }
    if (filtered > 1) {
        Buf listed;
        buf_init(&listed);
        size_t n = 0;
        for (size_t i = 0; i < all->n && n < 6; i++) {
            const Symbol *s = &all->v[i];
            if (strcmp(s->name, name) != 0 || !hint_ok(s, kind, scope))
                continue;
            describe_symbol(&listed, n++, s);
        }
        int rc = na_fail("ambiguous_symbol: %s (%zu matches: %s; narrow with kind or scope)", name, filtered,
                         listed.p);
        buf_free(&listed);
        buf_free(&cand);
        return rc;
    }
    /* The name is present. A kind/scope hint that misses the only declaration is not
     * symbol_not_found; the similar-list used to repeat that same name. */
    if (exact == 1) {
        buf_free(&cand);
        return copy_symbol(&all->v[exact_at], out);
    }
    if (exact > 1) {
        int rc = na_fail("ambiguous_symbol: %s (%zu matches: %s; narrow with kind or scope)", name, exact, cand.p);
        buf_free(&cand);
        return rc;
    }
    buf_free(&cand);
    buf_init(&cand);
    for (size_t i = 0; i < all->n; i++) {
        if (symbol_name_rank(all->v[i].name, name) != 1)
            continue;
        if (ci == 0)
            ci_at = (long)i;
        if (ci < 6)
            describe_symbol(&cand, ci, &all->v[i]);
        ci++;
    }
    if (ci == 1) {
        buf_free(&cand);
        return copy_symbol(&all->v[ci_at], out);
    }
    if (ci > 1) {
        int rc = na_fail("ambiguous_symbol: %s (%zu matches: %s; narrow with kind or scope)", name, ci, cand.p);
        buf_free(&cand);
        return rc;
    }
    buf_free(&cand);
    buf_init(&cand);
    size_t shown = 0;
    for (size_t i = 0; i < all->n && shown < 5; i++) {
        int rank = symbol_name_rank(all->v[i].name, name);
        if (rank < 2)
            continue;
        buf_printf(&cand, "%s%s", shown ? ", " : "", all->v[i].name);
        shown++;
    }
    int rc = shown ? na_fail("symbol_not_found: %s (similar: %s)", name, cand.p)
                   : na_fail("symbol_not_found: %s", name);
    buf_free(&cand);
    return rc;
}

struct OutlineCache {
    std::string path="snapshot.c";
    LanguageEngine *engine = language_new("");
    ~OutlineCache() { language_free(engine); }
};
OutlineCache *outline_cache_new() { return new OutlineCache; }
void outline_cache_free(OutlineCache *cache) { delete cache; }
int syntax_outline_cached(OutlineCache *cache, const Snapshot *snap, bool nested, SymbolList *out) {
    if (!cache || !cache->engine)
        return na_fail("native language engine unavailable");
    return language_outline(cache->engine, cache->path.c_str(), snap->text, snap->len, nested, out);
}
int syntax_outline(const Snapshot *snap, bool nested, SymbolList *out) {
    OutlineCache cache;
    return syntax_outline_cached(&cache, snap, nested, out);
}
int syntax_select_cached(OutlineCache *cache, const Snapshot *snap, const char *name, const char *kind,
                         const char *scope, Symbol *out) {
    SymbolList all{};
    if (syntax_outline_cached(cache, snap, true, &all) != 0)
        return -1;
    int rc = symbols_select(&all, name, kind, scope, out);
    symbols_free(&all);
    return rc;
}
int syntax_select(const Snapshot *snap, const char *name, const char *kind, const char *scope, Symbol *out) {
    OutlineCache cache;
    return syntax_select_cached(&cache, snap, name, kind, scope, out);
}
int syntax_replace_symbol_cached(OutlineCache *cache, const Snapshot *snap, const char *name,
                                 const char *replacement, size_t len, const char *kind, const char *scope,
                                 ChangeSet *out) {
    Symbol s{};
    if (syntax_select_cached(cache, snap, name, kind, scope, &s) != 0)
        return -1;
    cs_init(out);
    cs_add(out, s.start, s.end, replacement, len);
    free(s.name);
    free(s.kind);
    free(s.scope);
    return 0;
}
int syntax_replace_symbol(const Snapshot *snap, const char *name, const char *replacement, size_t len,
                          const char *kind, const char *scope, ChangeSet *out) {
    OutlineCache cache;
    return syntax_replace_symbol_cached(&cache, snap, name, replacement, len, kind, scope, out);
}

int syntax_outline_path(const char *path, const Snapshot *snap, bool nested, SymbolList *out) {
    OutlineCache cache;
    if (!cache.engine)
        return -1;
    return language_outline(cache.engine, path, snap->text, snap->len, nested, out);
}

void outline_cache_set_path(OutlineCache *cache,const char *path){cache->path=path;}
