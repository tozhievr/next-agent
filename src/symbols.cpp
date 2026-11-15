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

int symbols_select(const SymbolList *all, const char *name, const char *kind, const char *scope,
                   Symbol *out) {
    long found = -1;
    size_t count = 0;
    Buf cand;
    buf_init(&cand);
    for (size_t i = 0; i < all->n; i++) {
        const Symbol *s = &all->v[i];
        if (strcmp(s->name, name) != 0)
            continue;
        if (kind && strcmp(s->kind, kind) != 0)
            continue;
        if (scope && strcmp(s->scope, scope) != 0)
            continue;
        if (count == 0)
            found = (long)i;
        if (count < 6)
            buf_printf(&cand, "%s%s%s%s@%zu", count ? ", " : "", s->scope[0] ? s->scope : "",
                       s->scope[0] ? "::" : "", s->kind, s->line);
        count++;
    }
    if (count == 0) {
        /* help the caller: names that contain the query */
        size_t shown = 0, n = strlen(name);
        for (size_t i = 0; i < all->n && shown < 5; i++) {
            const char *h = all->v[i].name;
            for (size_t k = 0; h[k]; k++) {
                size_t j = 0;
                while (j < n && h[k + j] &&
                       tolower((unsigned char)h[k + j]) == tolower((unsigned char)name[j]))
                    j++;
                if (j == n) {
                    buf_printf(&cand, "%s%s", shown ? ", " : "", h);
                    shown++;
                    break;
                }
            }
        }
        int rc = shown ? na_fail("symbol_not_found: %s (similar: %s)", name, cand.p)
                       : na_fail("symbol_not_found: %s", name);
        buf_free(&cand);
        return rc;
    }
    if (count != 1) {
        int rc =
            na_fail("ambiguous_symbol: %s (%zu matches: %s; narrow with kind or scope)", name, count, cand.p);
        buf_free(&cand);
        return rc;
    }
    buf_free(&cand);
    const Symbol *s = &all->v[found];
    *out = *s;
    out->name = na_strdup(s->name);
    out->kind = na_strdup(s->kind);
    out->scope = na_strdup(s->scope);
    return 0;
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
