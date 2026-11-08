/* LSP foundation: position codec, framing, diagnostics comparison, compilation
 * database, dependency discovery, a clangd client and a pre-commit gate. */
#ifndef NA_LSP_H
#define NA_LSP_H
#include "c23.h"
#include "core.h"
#include "json.h"
#include "store.h"

/* ---- position codec (UTF-8 / UTF-16 / UTF-32) ---- */
typedef enum { ENC_UTF8, ENC_UTF16, ENC_UTF32 } PosEnc;
int pos_enc_parse(const char *name, PosEnc *out); /* "utf-8" | "utf-16" | "utf-32" */
/* `offset` counts codepoints. */
int lsp_offset_to_position(const char *text, size_t len, size_t offset, PosEnc enc, size_t *line,
                           size_t *character);
int lsp_position_to_offset(const char *text, size_t len, size_t line, size_t character, PosEnc enc,
                           size_t *offset);

/* ---- Content-Length framing ---- */
typedef struct {
    /* >0 bytes read, 0 timeout, -1 end of stream */
    int (*read)(void *ctx, void *buf, size_t cap, int timeout_ms);
    void *ctx;
    Buf pending;
    size_t pos;
} LspReader;

void lsp_reader_init(LspReader *r, int (*read)(void *, void *, size_t, int), void *ctx);
void lsp_reader_free(LspReader *r);
/* 1 = message in *out, 0 = timeout, 2 = clean EOF, -1 = error. */
int lsp_read_message(LspReader *r, int timeout_ms, J **out);
/* Frames the message into `out` (appended). */
int lsp_frame_message(const J *msg, Buf *out);

/* ---- diagnostics comparison ---- */
typedef struct {
    int severity;
    char *code, *message;
} DiagFingerprint;

typedef struct {
    DiagFingerprint *added, *removed;
    size_t nadded, nremoved;
    bool acceptable;
} DiagDelta;

int diag_compare(const J *before, const J *after, int maximum_severity, DiagDelta *out);
void diag_delta_free(DiagDelta *d);
/* Fails closed unless `report` is version-tagged with `expected_version`. */
int diag_require_fresh(const J *report, int64_t expected_version, const J **diagnostics);

/* ---- compilation database ---- */
typedef struct {
    char *source; /* workspace-relative */
    StrList dirs; /* absolute normalized include directories */
} CompEntry;

typedef struct {
    CompEntry *v;
    size_t n, cap;
} CompDb;

int comp_load(const char *root, size_t max_entries, CompDb *out);
const StrList *comp_dirs(const CompDb *db, const char *source);
void comp_free(CompDb *db);

/* ---- dependency discovery ---- */
typedef int (*TextFn)(void *ctx, const char *name, char **text, size_t *len);

typedef struct {
    size_t max_files;
    TextFn text;                  /* optional snapshot reader (workspace-relative name) */
    void *text_ctx;
    const StrList *snapshot_paths; /* optional unflushed documents */
    const StrList *include_dirs;   /* default include directories (absolute) */
    const CompDb *by_source;       /* optional per-translation-unit directories */
} DepOptions;

/* Translation units whose literal quoted-include closure reaches a changed header. */
int dependent_sources(const char *root, const StrList *changed, const DepOptions *opt, StrList *out);

/* ---- LSP client interface (clangd or a test double) ---- */
typedef struct {
    int (*open_document)(void *ctx, const char *abs_path, const char *text, size_t len, int64_t version,
                         char **uri);
    int (*change_document)(void *ctx, const char *uri, const char *text, size_t len, int64_t version);
    int (*wait_diagnostics)(void *ctx, const char *uri, int64_t version, J **report);
    int (*close_document)(void *ctx, const char *uri);
    void *ctx;
    /* Optional: does a server handle this file? NULL = every file. */
    bool (*supports)(void *ctx, const char *abs_path);
} LspOps;

/* A client of one language server process (clangd, pyright, rust-analyzer, ...). */
typedef struct LspClient LspClient;
typedef LspClient ClangdClient; /* historical name */
/* `args` is the raw command line tail; `exe` is looked up on PATH. */
LspClient *lsp_client_start(const char *root, const char *exe, const char *args, int timeout_ms);
LspClient *clangd_start(const char *root, const char *exe, int timeout_ms);
void clangd_close(LspClient *c);
const char *clangd_encoding(const LspClient *c);
LspOps clangd_ops(LspClient *c);
int clangd_request(LspClient *c, const char *method, J *params, J **result);
int clangd_formatting(LspClient *c, const char *uri, int tab_size, J **edits);
char *lsp_path_to_uri(const char *abs_path);
/* Overrides the languageId sent in didOpen (default: derived from the file extension). */
void lsp_client_set_language(LspClient *c, const char *language_id);
/* True when the server accepted ranged (incremental) didChange notifications. */
bool lsp_client_incremental(const LspClient *c);
/* Number of didChange notifications sent as a range / as whole text (diagnostics, tests). */
size_t lsp_client_ranged_changes(const LspClient *c);
size_t lsp_client_full_changes(const LspClient *c);
/* Opens (or incrementally updates) a document and returns its URI (malloc'd). */
int lsp_client_sync(LspClient *c, const char *abs_path, const char *text, size_t len, char **uri);
bool lsp_client_settle(LspClient *c, const char *abs_path, int budget_ms);
const char *lsp_language_id(const char *path); /* "c", "python", ... or "plaintext" */

/* ---- language server hub: one server per language family, started on demand ---- */
typedef struct LspHub LspHub;
/* Reads <root>/.aed/lsp.json (optional) on top of the built-in server table. `clangd_exe` (optional)
 * replaces the C/C++ server command. */
LspHub *lsp_hub_new(const char *root, const char *clangd_exe, int timeout_ms);
void lsp_hub_free(LspHub *h);
LspOps lsp_hub_ops(LspHub *h);
bool lsp_hub_handles(LspHub *h, const char *path); /* a configured, installable server exists */
bool lsp_hub_configured(LspHub *h, const char *path); /* an entry for this extension exists (nothing is started) */
LspClient *lsp_hub_client(LspHub *h, const char *path); /* the running client for a file, or NULL */
/* Top-level (or all, with `nested`) symbols of a document from textDocument/documentSymbol. */
int lsp_hub_outline(LspHub *h, const char *abs_path, const char *text, size_t len, bool nested, SymbolList *out);
/* [{name, extensions, command, available, running}] */
J *lsp_hub_status(LspHub *h);

/* ---- semantic requests (any server that implements them) ---- */
typedef struct {
    char *path; /* absolute, normalized */
    size_t sl, sc, el, ec; /* LSP line/character range in the server's position encoding */
    char *text;            /* replacement text for edits; NULL for plain locations */
} LspLoc;
typedef struct {
    LspLoc *v;
    size_t n, cap;
} LspLocs;
void lsp_locs_free(LspLocs *l);
char *lsp_uri_to_path(const char *uri); /* file:// URI -> normalized absolute path, malloc'd */
PosEnc lsp_hub_encoding(LspHub *h, const char *abs_path);
/* Opens or incrementally updates a document in the server that owns it. */
int lsp_hub_sync(LspHub *h, const char *abs_path, const char *text, size_t len);
/* Waits (bounded) until the server has analysed the synced text, so cross-file queries see it. */
void lsp_hub_settle(LspHub *h, const char *abs_path);
/* textDocument/references at a codepoint offset of the (already synced) document. */
int lsp_hub_references(LspHub *h, const char *abs_path, const char *text, size_t len, size_t offset,
                       bool include_declaration, LspLocs *out);
/* textDocument/rename: the resulting WorkspaceEdit flattened to text edits (path + range + text). */
int lsp_hub_rename(LspHub *h, const char *abs_path, const char *text, size_t len, size_t offset,
                   const char *new_name, LspLocs *out);

/* ---- pre-commit diagnostic gate ---- */
typedef struct DiagnosticGate DiagnosticGate;
DiagnosticGate *gate_new(const char *root, LspOps ops, Store *store, int maximum_severity);
void gate_free(DiagnosticGate *g);
Validator gate_validator(DiagnosticGate *g);

#endif
