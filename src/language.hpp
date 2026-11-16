/* Native language integration, position codecs, framing and diagnostic validation. */
#ifndef NA_LANGUAGE_HPP
#define NA_LANGUAGE_HPP
#include "symbols.hpp"
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

/* ---- synchronous diagnostic provider (native parser or a test double) ---- */
typedef struct {
    int (*open_document)(void *ctx, const char *abs_path, const char *text, size_t len, int64_t version,
                         char **uri);
    int (*change_document)(void *ctx, const char *uri, const char *text, size_t len, int64_t version);
    int (*wait_diagnostics)(void *ctx, const char *uri, int64_t version, J **report);
    int (*close_document)(void *ctx, const char *uri);
    void *ctx;
    /* Optional: does a server handle this file? NULL = every file. */
    bool (*supports)(void *ctx, const char *abs_path);
} DiagnosticOps;

/* ---- in-process language engine: one registry, lazily selected file modules ---- */
typedef struct LanguageEngine LanguageEngine;
/* One native registry and persistent parse sessions; selects plugins from file extensions. */
LanguageEngine *language_new(const char *root);
void language_free(LanguageEngine *h);
/* Retain richer C declaration ranges after native parsing. */
DiagnosticOps language_ops(LanguageEngine *h);
bool language_handles(LanguageEngine *h, const char *path); /* a configured, installable server exists */
bool language_configured(LanguageEngine *h, const char *path); /* an entry for this extension exists (nothing is started) */
/* Top-level (or all, with `nested`) symbols of a document from textDocument/documentSymbol. */
int language_outline(LanguageEngine *h, const char *abs_path, const char *text, size_t len, bool nested, SymbolList *out);
/* [{name, extensions, mode, available, loaded}] */
J *language_status(LanguageEngine *h);
/* Native grammar rule nodes with codepoint ranges, identity fingerprints and parent links. */
int language_nodes(LanguageEngine *h,const char *path,const char *text,size_t len,J **out,const J *filter=nullptr);

/* ---- semantic requests (any server that implements them) ---- */
typedef struct {
    char *path; /* absolute, normalized */
    size_t sl, sc, el, ec; /* LSP line/character range in the server's position encoding */
    char *text;            /* replacement text for edits; NULL for plain locations */
} SymbolLocation;
typedef struct {
    SymbolLocation *v;
    size_t n, cap;
} SymbolLocations;
void symbol_locations_free(SymbolLocations *l);
PosEnc language_encoding(LanguageEngine *h, const char *abs_path);
/* Opens or incrementally updates a document in the server that owns it. */
int language_sync(LanguageEngine *h, const char *abs_path, const char *text, size_t len);
/* Waits (bounded) until the server has analysed the synced text, so cross-file queries see it. */
void language_settle(LanguageEngine *h, const char *abs_path);
/* textDocument/references at a codepoint offset of the (already synced) document. */
int language_references(LanguageEngine *h, const char *abs_path, const char *text, size_t len, size_t offset,
                       bool include_declaration, SymbolLocations *out);
/* textDocument/rename: the resulting WorkspaceEdit flattened to text edits (path + range + text). */
int language_rename(LanguageEngine *h, const char *abs_path, const char *text, size_t len, size_t offset,
                   const char *new_name, SymbolLocations *out);

/* ---- pre-commit diagnostic gate ---- */
typedef struct DiagnosticGate DiagnosticGate;
DiagnosticGate *gate_new(const char *root, DiagnosticOps ops, Store *store, int maximum_severity);
void gate_free(DiagnosticGate *g);
Validator gate_validator(DiagnosticGate *g);

#endif
