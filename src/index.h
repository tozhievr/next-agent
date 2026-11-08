/* Workspace index: symbols (from the language servers, or the C scanner as a fallback) and an
 * identifier inverted index for every source file below the root. Files are re-extracted only when
 * their content changed, so refreshing a large workspace after a small edit is cheap. */
#ifndef NA_INDEX_H
#define NA_INDEX_H
#include "lsp.h"
#include "store.h"

typedef struct WsIndex WsIndex;

WsIndex *wsi_new(Store *st, LspHub *hub); /* hub may be NULL: C-family files use the scanner */
void wsi_free(WsIndex *ix);
/* Walks the workspace (plus unflushed store documents) and re-extracts the files that changed. */
int wsi_refresh(WsIndex *ix);
size_t wsi_extracted(const WsIndex *ix); /* files re-extracted by the last refresh */
size_t wsi_skipped(const WsIndex *ix);   /* files recognised as unchanged by size + mtime, without being read */
uint64_t wsi_hash(const WsIndex *ix, size_t i); /* content hash of file i (changes when the text changes) */
/* One caller-owned attachment (the hybrid search cache), freed with the index. */
void *wsi_user(const WsIndex *ix);
void wsi_set_user(WsIndex *ix, void *p, void (*free_fn)(void *));

size_t wsi_nfiles(const WsIndex *ix);
const char *wsi_path(const WsIndex *ix, size_t i);          /* workspace-relative */
const SymbolList *wsi_symbols(const WsIndex *ix, size_t i); /* nested symbols */
bool wsi_from_server(const WsIndex *ix, size_t i);          /* symbols came from a language server */
/* Does file i contain the identifier `name` anywhere (comments and strings included)? */
bool wsi_has_ident(const WsIndex *ix, size_t i, const char *name);

#endif
