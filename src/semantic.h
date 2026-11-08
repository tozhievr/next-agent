/* Semantic refactorings driven by language servers and the workspace index: a symbol is resolved to
 * one definition through the index, the server answers references / rename for exactly that
 * definition, and every change is applied as one transaction through the store. */
#ifndef NA_SEMANTIC_H
#define NA_SEMANTIC_H
#include "index.h"

typedef struct {
    Store *st;
    LspHub *hub;
    WsIndex *ix;
} Sem;

/* `kind`, `scope` and `path` (workspace-relative) narrow the definition; all may be NULL.
 * Errors: symbol_not_found, ambiguous_symbol, no_language_server. */
int sem_references(Sem *s, const char *name, const char *kind, const char *scope, const char *path, J **out);
int sem_rename(Sem *s, const char *name, const char *kind, const char *scope, const char *path,
               const char *new_name, int64_t if_rev, const char *label, const char *session,
               const Validator *validator, EditResult *res, size_t *sites);
/* Symbols of the whole workspace whose name contains `query` (case-insensitive). */
int sem_find_symbols(Sem *s, const char *query, const char *kind, size_t limit, J **out);

#endif
