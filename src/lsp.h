/* LSP foundation: position codec, framing, diagnostics comparison, compilation
 * database, dependency discovery, a clangd client and a pre-commit gate. */
#ifndef NA_LSP_H
#define NA_LSP_H
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
} LspOps;

typedef struct ClangdClient ClangdClient;
ClangdClient *clangd_start(const char *root, const char *exe, int timeout_ms);
void clangd_close(ClangdClient *c);
const char *clangd_encoding(const ClangdClient *c);
LspOps clangd_ops(ClangdClient *c);
int clangd_request(ClangdClient *c, const char *method, J *params, J **result);
int clangd_formatting(ClangdClient *c, const char *uri, int tab_size, J **edits);
char *lsp_path_to_uri(const char *abs_path);

/* ---- pre-commit diagnostic gate ---- */
typedef struct DiagnosticGate DiagnosticGate;
DiagnosticGate *gate_new(const char *root, LspOps ops, Store *store, int maximum_severity);
void gate_free(DiagnosticGate *g);
Validator gate_validator(DiagnosticGate *g);

#endif
