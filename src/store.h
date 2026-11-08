/* Durable revision store: append-only journal (.aed/journal.aedj), no SQLite.
 *
 * Every commit is one checksummed journal record; torn tails are discarded on
 * replay. Writers serialize through an exclusive lock file and re-read the
 * journal tail first, so several processes can share one workspace. */
#ifndef NA_STORE_H
#define NA_STORE_H
#include "core.h"
#include "json.h"

typedef struct Store Store;

#define STORE_NO_REV ((int64_t)-1)

typedef struct {
    const char *path;
    const Snapshot *before, *after;
} ValidateDoc;

/* Pre-commit validator. `validate` runs outside the write lock; `recheck`
 * (optional) runs again inside it, so speculative validation can be rejected
 * if the workspace changed in between. Both return 0 or -1 with na_errmsg set. */
typedef struct {
    int (*validate)(void *ctx, const ValidateDoc *docs, size_t n);
    int (*recheck)(void *ctx);
    void *ctx;
} Validator;

typedef struct {
    int64_t rev;
    StrList paths;
} EditResult;

Store *store_open(const char *root);
void store_close(Store *st);
const char *store_root(const Store *st);

/* Canonical workspace-relative path, malloc'd; NULL on error. */
char *store_canon(const Store *st, const char *name);

int store_view(Store *st, const char *name, Snapshot **out);
int64_t store_head(Store *st);
/* Current text without recording anything (unknown files are read from disk). */
int store_snapshot_text(Store *st, const char *name, char **text, size_t *len);
/* Sorted paths of documents that carry unflushed or committed revisions. */
int store_snapshot_paths(Store *st, StrList *out);

/* `changes[i]` must already be sorted/validated; ownership stays with caller. */
int store_edit(Store *st, const char *const *names, const ChangeSet *changes, size_t n,
               int64_t if_rev, const char *label, const char *session, const Validator *validator,
               EditResult *result);
/* Undo revision `rev` (0 = head). Non-head revisions are rebased over later
 * changes and rejected with undo_conflict when they overlap. */
int store_undo(Store *st, int64_t rev, const Validator *validator, EditResult *result);
/* Re-apply the most recently undone head revision. */
int store_redo(Store *st, const Validator *validator, EditResult *result);
J *store_history(Store *st, int limit);
/* What a revision changed (rev 0 = head): {rev,label,files:[{path,hunks:[{line?,offset,removed,added}]}]}. */
int store_rev_diff(Store *st, int64_t rev, J **out);
/* `paths` NULL = all documents. */
int store_flush(Store *st, const char *const *paths, size_t npaths, StrList *written);
/* Reconciles a document whose file changed outside NextAgent (what `sync` reports as disk_conflict) by a
 * line-based three-way merge. strategy: auto | ours | theirs. *report: {path, status, conflicts?, rev?, paths?}. */
int store_merge_external(Store *st, const char *name, const char *strategy, bool dry_run, J **report);
void edit_result_free(EditResult *r);

#endif
