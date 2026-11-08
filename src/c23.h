/* C23 syntax-only navigation: tolerant top-level scanner (replaces tree-sitter).
 *
 * Recognizes syntactic declarations, not resolved symbol identities. */
#ifndef NA_C23_H
#define NA_C23_H
#include "core.h"
#include "json.h"

typedef struct {
    char *name, *kind, *scope;
    size_t start, end; /* codepoint offsets */
    size_t line, column;
} Symbol;

typedef struct {
    Symbol *v;
    size_t n, cap;
} SymbolList;

void symbols_free(SymbolList *l);
J *symbol_to_json(const Symbol *s);

int c23_outline(const Snapshot *snap, bool nested, SymbolList *out);

/* Incremental variant: the cache remembers the previous text of one document and, on the next
 * call, re-lexes only the window around the change, reusing every unchanged top-level item. */
typedef struct OutlineCache OutlineCache;
OutlineCache *outline_cache_new(void);
void outline_cache_free(OutlineCache *c);
size_t outline_cache_last_tokens(const OutlineCache *c); /* tokens lexed by the last update */
size_t outline_cache_last_reused(const OutlineCache *c); /* items reused by the last update */
size_t outline_cache_first_divergence(const OutlineCache *c, const Snapshot *snap); /* test hook */
int c23_outline_cached(OutlineCache *c, const Snapshot *snap, bool nested, SymbolList *out);
int c23_select_cached(OutlineCache *c, const Snapshot *snap, const char *name, const char *kind,
                      const char *scope, Symbol *out);
int c23_replace_symbol_cached(OutlineCache *c, const Snapshot *snap, const char *name, const char *replacement,
                              size_t rlen, const char *kind, const char *scope, ChangeSet *out);
/* kind and scope may be NULL. Errors: symbol_not_found / ambiguous_symbol. */
int c23_select(const Snapshot *snap, const char *name, const char *kind, const char *scope, Symbol *out);
int c23_replace_symbol(const Snapshot *snap, const char *name, const char *replacement, size_t rlen,
                       const char *kind, const char *scope, ChangeSet *out);

#endif
