/* Workspace refactorings (M3): references, rename, add_param, add_include,
 * find_symbols.
 *
 * Everything here is *syntactic*: identifiers are matched by spelling after
 * comments, string/char literals and #include targets are skipped. There is no
 * name resolution, so a local variable that shares a spelling with a function
 * is treated as the same symbol. Callers should review the reported
 * references (or rely on the clangd gate) for ambiguous names. */
#ifndef NA_REFACTOR_H
#define NA_REFACTOR_H
#include "json.h"
#include "store.h"

/* Files considered: C/C++ sources and headers on disk below the root plus
 * documents carrying revisions in the store. `only` (optional) restricts the
 * scan to those workspace-relative paths. */
int rf_references(Store *st, const char *name, const StrList *only, bool members, J **out);

typedef struct {
    size_t sites;       /* edits made (rename) or declarations (add_param) */
    size_t calls;       /* add_param: call sites updated */
} RfCounts;

int rf_rename(Store *st, const char *name, const char *new_name, const StrList *only, bool members,
              bool allow_existing, int64_t if_rev, const char *label, const char *session,
              const Validator *validator, EditResult *res, RfCounts *counts);

/* `position` is the 0-based index of the new parameter, or -1 to append
 * (before a trailing `...`). `default_arg` is inserted at every call site. */
int rf_add_param(Store *st, const char *function, const char *param, const char *default_arg,
                 int64_t position, const StrList *only, int64_t if_rev, const char *label,
                 const char *session, const Validator *validator, EditResult *res, RfCounts *counts);

/* Identifier occurrence (workspace-relative path, codepoint offset of its first character). */
typedef struct {
    const char *path;
    size_t start;
} RfSite;

/* Like rf_add_param, but only the given occurrences (references of one symbol resolved by a language
 * server) are declarations or call sites; same-spelled identifiers elsewhere are left alone. */
int rf_add_param_sites(Store *st, const char *function, const char *param, const char *default_arg,
                       int64_t position, const RfSite *sites, size_t nsites, int64_t if_rev, const char *label,
                       const char *session, const Validator *validator, EditResult *res, RfCounts *counts);

/* Inserts `#include` after the last existing include (or the include guard).
 * `*changed` is false when the header was already included (nothing committed). */
int rf_add_include(Store *st, const char *target, const char *header, bool system, int64_t if_rev,
                   const char *label, const char *session, const Validator *validator,
                   EditResult *res, bool *changed);

/* Top-level symbols across the workspace whose name contains `query`
 * (case-insensitive). `kind` optional. */
int rf_find_symbols(Store *st, const char *query, const char *kind, size_t limit, J **out);

#endif
