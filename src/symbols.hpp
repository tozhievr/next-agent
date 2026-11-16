/* Native syntax navigation backed exclusively by next-langserver.
 *
 * Recognizes syntactic declarations, not resolved symbol identities. */
#ifndef NA_SYMBOLS_HPP
#define NA_SYMBOLS_HPP
#include "core.h"
#include "json.h"

typedef struct {
    char *name, *kind, *scope;
    size_t start, end; /* codepoint offsets */
    size_t sel;        /* codepoint offset of the name token, or (size_t)-1 when unknown */
    size_t line, column;
} Symbol;

typedef struct {
    Symbol *v;
    size_t n, cap;
} SymbolList;

void symbols_free(SymbolList *l);
/* Unique match by name (and optional kind/scope) in an outline. Errors: symbol_not_found / ambiguous_symbol.
 */
int symbols_select(const SymbolList *all, const char *name, const char *kind, const char *scope, Symbol *out);
J *symbol_to_json(const Symbol *s);

int syntax_outline_path(const char *path, const Snapshot *snap, bool nested, SymbolList *out);
int syntax_outline(const Snapshot *snap, bool nested, SymbolList *out);

/* Persistent next-langserver parse session for C snapshot compatibility calls. */
typedef struct OutlineCache OutlineCache;
OutlineCache *outline_cache_new(void);
void outline_cache_free(OutlineCache *c);
void outline_cache_set_path(OutlineCache *c, const char *path);
int syntax_outline_cached(OutlineCache *c, const Snapshot *snap, bool nested, SymbolList *out);
int syntax_select_cached(OutlineCache *c, const Snapshot *snap, const char *name, const char *kind,
                         const char *scope, Symbol *out);
int syntax_replace_symbol_cached(OutlineCache *c, const Snapshot *snap, const char *name,
                                 const char *replacement, size_t rlen, const char *kind, const char *scope,
                                 ChangeSet *out);
/* kind and scope may be NULL. Errors: symbol_not_found / ambiguous_symbol. */
int syntax_select(const Snapshot *snap, const char *name, const char *kind, const char *scope, Symbol *out);
int syntax_replace_symbol(const Snapshot *snap, const char *name, const char *replacement, size_t rlen,
                          const char *kind, const char *scope, ChangeSet *out);

#endif
