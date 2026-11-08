/* Language server hub: maps file extensions to language servers (clangd, pyright, rust-analyzer,
 * gopls, ...), starts them on demand, and turns documentSymbol answers into NextAgent symbols. */
#include "lsp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    StrList exes, args; /* candidates in preference order (parallel lists) */
    StrList exts;
    char *lang; /* languageId override */
    LspClient *client;
    bool tried;
    char *error;
} Server;

typedef struct {
    char *uri;
    Server *srv;
} UriMap;

struct LspHub {
    char *root;
    int timeout_ms;
    Server *v;
    size_t n, cap;
    UriMap *uris;
    size_t nuris, capuris;
    size_t clangd_idx; /* the built-in C/C++ entry */
    bool scanner_c;    /* C-family files stay with the built-in scanner (one-shot CLI) */
};

static Server *add_server(LspHub *h, const char *name, const char *lang) {
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 8;
        h->v = na_realloc(h->v, h->cap * sizeof *h->v);
    }
    Server *s = &h->v[h->n++];
    memset(s, 0, sizeof *s);
    s->name = na_strdup(name);
    s->lang = lang ? na_strdup(lang) : NULL;
    sl_init(&s->exes);
    sl_init(&s->args);
    sl_init(&s->exts);
    return s;
}

static void cand(Server *s, const char *exe, const char *args) {
    sl_push(&s->exes, exe);
    sl_push(&s->args, args);
}

static void exts(Server *s, const char *const *list) {
    for (size_t i = 0; list[i]; i++) sl_push(&s->exts, list[i]);
}

static void load_config(LspHub *h) {
    char *p = NULL;
    Buf pb;
    buf_init(&pb);
    buf_printf(&pb, "%s/.aed/lsp.json", h->root);
    p = buf_take(&pb);
    Buf data;
    buf_init(&data);
    int rc = pf_read_file(p, &data);
    free(p);
    if (rc != 0 || data.len == 0) {
        buf_free(&data);
        return;
    }
    J *cfg = json_parse(data.p, data.len);
    buf_free(&data);
    const J *arr = cfg ? j_get(cfg, "servers") : NULL;
    if (arr && arr->k == J_ARR) {
        for (size_t i = 0; i < arr->n; i++) {
            const J *e = arr->items[i];
            const J *name = j_get(e, "name"), *cmd = j_get(e, "command"), *args = j_get(e, "args");
            const J *xs = j_get(e, "extensions"), *lang = j_get(e, "language");
            if (!j_is_str(cmd) || !xs || xs->k != J_ARR) continue;
            Server *s = add_server(h, j_is_str(name) ? name->s : cmd->s, j_is_str(lang) ? lang->s : NULL);
            cand(s, cmd->s, j_is_str(args) ? args->s : "");
            for (size_t k = 0; k < xs->n; k++)
                if (j_is_str(xs->items[k])) sl_push(&s->exts, xs->items[k]->s);
        }
    }
    json_free(cfg);
}

LspHub *lsp_hub_new(const char *root, const char *clangd_exe, int timeout_ms) {
    LspHub *h = na_alloc(sizeof *h);
    h->root = path_normalize(root);
    h->timeout_ms = timeout_ms > 0 ? timeout_ms : 20000;
    load_config(h); /* user entries win over the built-ins below */
    static const char *const c_exts[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", NULL};
    static const char *const py_exts[] = {".py", ".pyi", NULL};
    static const char *const ts_exts[] = {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs", NULL};
    static const char *const rs_exts[] = {".rs", NULL};
    static const char *const go_exts[] = {".go", NULL};
    static const char *const zig_exts[] = {".zig", NULL};
    static const char *const lua_exts[] = {".lua", NULL};
    h->clangd_idx = h->n;
    Server *s = add_server(h, "clangd", NULL);
    cand(s, clangd_exe ? clangd_exe : "clangd", "--log=error --background-index=false --header-insertion=never");
    exts(s, c_exts);
    s = add_server(h, "python", NULL);
    cand(s, "pyright-langserver", "--stdio");
    cand(s, "basedpyright-langserver", "--stdio");
    cand(s, "pylsp", "");
    exts(s, py_exts);
    s = add_server(h, "rust-analyzer", NULL);
    cand(s, "rust-analyzer", "");
    exts(s, rs_exts);
    s = add_server(h, "gopls", NULL);
    cand(s, "gopls", "");
    exts(s, go_exts);
    s = add_server(h, "typescript", NULL);
    cand(s, "typescript-language-server", "--stdio");
    exts(s, ts_exts);
    s = add_server(h, "zls", NULL);
    cand(s, "zls", "");
    exts(s, zig_exts);
    s = add_server(h, "lua", NULL);
    cand(s, "lua-language-server", "");
    exts(s, lua_exts);
    return h;
}

void lsp_hub_free(LspHub *h) {
    if (!h) return;
    for (size_t i = 0; i < h->n; i++) {
        Server *s = &h->v[i];
        clangd_close(s->client);
        free(s->name);
        free(s->lang);
        free(s->error);
        sl_free(&s->exes);
        sl_free(&s->args);
        sl_free(&s->exts);
    }
    free(h->v);
    for (size_t i = 0; i < h->nuris; i++) free(h->uris[i].uri);
    free(h->uris);
    free(h->root);
    free(h);
}

void lsp_hub_scanner_for_c(LspHub *h) { h->scanner_c = true; }

static Server *match(LspHub *h, const char *path) {
    for (size_t i = 0; i < h->n; i++) {
        if (h->scanner_c && i == h->clangd_idx) continue;
        for (size_t k = 0; k < h->v[i].exts.n; k++)
            if (path_has_suffix_ci(path, h->v[i].exts.v[k])) return &h->v[i];
    }
    return NULL;
}

/* Returns the running client for the file, starting its server on first use. */
static LspClient *client_for(LspHub *h, const char *path, Server **srv_out) {
    Server *s = match(h, path);
    if (srv_out) *srv_out = s;
    if (!s) return NULL;
    if (!s->tried) {
        s->tried = true;
        for (size_t i = 0; i < s->exes.n && !s->client; i++) {
            char *found = pf_which(s->exes.v[i]);
            if (!found) continue;
            free(found);
            s->client = lsp_client_start(h->root, s->exes.v[i], s->args.v[i], h->timeout_ms);
            if (!s->client) s->error = na_strdup(na_errmsg());
        }
        if (!s->client && !s->error) s->error = na_strdup("no language server executable found on PATH");
        if (s->client && s->lang) lsp_client_set_language(s->client, s->lang);
    }
    return s->client;
}

bool lsp_hub_handles(LspHub *h, const char *path) { return client_for(h, path, NULL) != NULL; }

bool lsp_hub_configured(LspHub *h, const char *path) { return match(h, path) != NULL; }

/* ---------------------------------------------------------------- LspOps */
static void remember(LspHub *h, const char *uri, Server *s) {
    for (size_t i = 0; i < h->nuris; i++)
        if (strcmp(h->uris[i].uri, uri) == 0) {
            h->uris[i].srv = s;
            return;
        }
    if (h->nuris == h->capuris) {
        h->capuris = h->capuris ? h->capuris * 2 : 16;
        h->uris = na_realloc(h->uris, h->capuris * sizeof *h->uris);
    }
    h->uris[h->nuris].uri = na_strdup(uri);
    h->uris[h->nuris].srv = s;
    h->nuris++;
}

static LspClient *by_uri(LspHub *h, const char *uri) {
    for (size_t i = 0; i < h->nuris; i++)
        if (strcmp(h->uris[i].uri, uri) == 0) return h->uris[i].srv->client;
    na_seterr("no language server session for %s", uri);
    return NULL;
}

static int hub_open(void *ctx, const char *abs_path, const char *text, size_t len, int64_t version, char **uri) {
    LspHub *h = ctx;
    Server *s;
    LspClient *c = client_for(h, abs_path, &s);
    if (!c) return na_fail("no_language_server: %s (%s)", abs_path, s && s->error ? s->error : "no server configured");
    LspOps o = clangd_ops(c);
    int rc = o.open_document(o.ctx, abs_path, text, len, version, uri);
    if (rc == 0) remember(h, *uri, s);
    return rc;
}

static int hub_change(void *ctx, const char *uri, const char *text, size_t len, int64_t version) {
    LspClient *c = by_uri(ctx, uri);
    if (!c) return -1;
    LspOps o = clangd_ops(c);
    return o.change_document(o.ctx, uri, text, len, version);
}

static int hub_wait(void *ctx, const char *uri, int64_t version, J **report) {
    LspHub *h = ctx;
    LspClient *c = by_uri(ctx, uri);
    if (!c) return -1;
    if (c != h->v[h->clangd_idx].client) return lsp_client_wait_lenient(c, uri, version, report);
    LspOps o = clangd_ops(c);
    return o.wait_diagnostics(o.ctx, uri, version, report);
}

static int hub_close(void *ctx, const char *uri) {
    LspClient *c = by_uri(ctx, uri);
    if (!c) return -1;
    LspOps o = clangd_ops(c);
    return o.close_document(o.ctx, uri);
}

static bool hub_supports(void *ctx, const char *abs_path) { return lsp_hub_handles(ctx, abs_path); }

LspOps lsp_hub_ops(LspHub *h) {
    LspOps o;
    o.open_document = hub_open;
    o.change_document = hub_change;
    o.wait_diagnostics = hub_wait;
    o.close_document = hub_close;
    o.ctx = h;
    o.supports = hub_supports;
    return o;
}

/* ---------------------------------------------------------------- symbols */
static const char *const KIND_NAMES[] = {
    "file",     "module",   "namespace", "package",   "class",     "method",    "property", "field",
    "constructor", "enum",  "interface", "function",  "variable",  "constant",  "string",   "number",
    "boolean",  "array",    "object",    "key",       "null",      "enummember", "struct",  "event",
    "operator", "typeparameter",
};

typedef struct {
    const char *text;
    size_t len;
    PosEnc enc;
    SymbolList *out;
    bool nested;
} Conv;

static bool pos_to_offset(const Conv *cv, const J *pos, size_t *off) {
    const J *l = pos ? j_get(pos, "line") : NULL, *ch = pos ? j_get(pos, "character") : NULL;
    if (!j_is_int(l) || !j_is_int(ch) || l->i < 0 || ch->i < 0) return false;
    return lsp_position_to_offset(cv->text, cv->len, (size_t)l->i, (size_t)ch->i, cv->enc, off) == 0;
}

static void add_symbol(Conv *cv, const J *item, const char *scope, const J *range, const J *sel) {
    const J *name = j_get(item, "name"), *kind = j_get(item, "kind");
    if (!j_is_str(name) || !range) return;
    size_t s, e;
    if (!pos_to_offset(cv, j_get(range, "start"), &s) || !pos_to_offset(cv, j_get(range, "end"), &e) || e < s) return;
    SymbolList *out = cv->out;
    if (out->n == out->cap) {
        out->cap = out->cap ? out->cap * 2 : 16;
        out->v = na_realloc(out->v, out->cap * sizeof *out->v);
    }
    Symbol *sym = &out->v[out->n++];
    int k = j_is_int(kind) ? (int)kind->i : 0;
    sym->name = na_strdup(name->s);
    sym->kind = na_strdup(k >= 1 && k <= 26 ? KIND_NAMES[k - 1] : "symbol");
    sym->scope = na_strdup(scope);
    sym->start = s;
    sym->end = e;
    sym->sel = (size_t)-1;
    {
        size_t so;
        if (sel && pos_to_offset(cv, j_get(sel, "start"), &so) && so >= s && so <= e) sym->sel = so;
    }
    const J *sl = j_get(j_get(range, "start"), "line"), *sc = j_get(j_get(range, "start"), "character");
    sym->line = (size_t)sl->i + 1;
    sym->column = (size_t)sc->i;
}

static void walk(Conv *cv, const J *arr, const char *scope, int depth) {
    if (!arr || arr->k != J_ARR) return;
    for (size_t i = 0; i < arr->n; i++) {
        const J *it = arr->items[i];
        const J *children = j_get(it, "children");
        const J *range = j_get(it, "range");
        const J *loc = j_get(it, "location");
        if (!range && loc) range = j_get(loc, "range"); /* SymbolInformation */
        const J *container = j_get(it, "containerName");
        const char *sc = scope;
        if (!children && j_is_str(container)) sc = container->s;
        bool top_level = depth == 0 && !sc[0];
        if (top_level || cv->nested) add_symbol(cv, it, sc, range, j_get(it, "selectionRange"));
        if (children) {
            const J *name = j_get(it, "name");
            Buf b;
            buf_init(&b);
            if (scope[0]) buf_printf(&b, "%s::", scope);
            if (j_is_str(name)) buf_puts(&b, name->s);
            char *inner = buf_take(&b);
            walk(cv, children, inner, depth + 1);
            free(inner);
        }
    }
}

int lsp_hub_outline(LspHub *h, const char *abs_path, const char *text, size_t len, bool nested, SymbolList *out) {
    memset(out, 0, sizeof *out);
    Server *s;
    LspClient *c = client_for(h, abs_path, &s);
    if (!c) return na_fail("no_language_server: %s (%s)", abs_path, s && s->error ? s->error : "no server configured");
    char *uri = NULL;
    if (lsp_client_sync(c, abs_path, text, len, &uri) != 0) return -1;
    J *doc = j_obj();
    j_set(doc, "uri", j_str(uri));
    free(uri);
    J *params = j_obj();
    j_set(params, "textDocument", doc);
    J *res = NULL;
    if (clangd_request(c, "textDocument/documentSymbol", params, &res) != 0) return -1;
    Conv cv;
    cv.text = text;
    cv.len = len;
    cv.enc = ENC_UTF16;
    pos_enc_parse(clangd_encoding(c), &cv.enc);
    cv.out = out;
    cv.nested = nested;
    walk(&cv, res, "", 0);
    json_free(res);
    return 0;
}

J *lsp_hub_status(LspHub *h) {
    J *arr = j_arr();
    for (size_t i = 0; i < h->n; i++) {
        Server *s = &h->v[i];
        J *o = j_obj();
        j_set(o, "name", j_str(s->name));
        J *xs = j_arr();
        for (size_t k = 0; k < s->exts.n; k++) j_push(xs, j_str(s->exts.v[k]));
        j_set(o, "extensions", xs);
        const char *found = NULL;
        char *path = NULL;
        for (size_t k = 0; k < s->exes.n && !path; k++) {
            path = pf_which(s->exes.v[k]);
            if (path) found = s->exes.v[k];
        }
        j_set(o, "command", j_str(found ? found : (s->exes.n ? s->exes.v[0] : "")));
        j_set(o, "available", j_bool(path != NULL));
        j_set(o, "running", j_bool(s->client != NULL));
        if (s->error) j_set(o, "error", j_str(s->error));
        free(path);
        j_push(arr, o);
    }
    return arr;
}

LspClient *lsp_hub_client(LspHub *h, const char *path) { return client_for(h, path, NULL); }

/* ---------------------------------------------------------------- semantic requests */
void lsp_locs_free(LspLocs *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].path);
        free(l->v[i].text);
    }
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *lsp_uri_to_path(const char *uri) {
    if (strncmp(uri, "file://", 7) != 0) return NULL;
    const char *p = uri + 7;
    Buf b;
    buf_init(&b);
    for (; *p; p++) {
        int hi, lo;
        if (*p == '%' && (hi = hexval(p[1])) >= 0 && (lo = hexval(p[2])) >= 0) {
            buf_putc(&b, (char)(hi * 16 + lo));
            p += 2;
        } else {
            buf_putc(&b, *p);
        }
    }
    char *raw = buf_take(&b);
    const char *q = raw;
    if (q[0] == '/' && ((q[1] >= 'a' && q[1] <= 'z') || (q[1] >= 'A' && q[1] <= 'Z')) && q[2] == ':') q++;
    char *norm = path_normalize(q);
    free(raw);
    return norm;
}

PosEnc lsp_hub_encoding(LspHub *h, const char *abs_path) {
    PosEnc enc = ENC_UTF16;
    LspClient *c = client_for(h, abs_path, NULL);
    if (c) pos_enc_parse(clangd_encoding(c), &enc);
    return enc;
}

int lsp_hub_sync(LspHub *h, const char *abs_path, const char *text, size_t len) {
    Server *s;
    LspClient *c = client_for(h, abs_path, &s);
    if (!c) return na_fail("no_language_server: %s (%s)", abs_path, s && s->error ? s->error : "no server configured");
    char *uri = NULL;
    if (lsp_client_sync(c, abs_path, text, len, &uri) != 0) return -1;
    free(uri);
    return 0;
}

void lsp_hub_settle(LspHub *h, const char *abs_path) {
    LspClient *c = client_for(h, abs_path, NULL);
    if (c) lsp_client_settle(c, abs_path, 8000);
}

static void loc_add(LspLocs *l, char *path, size_t sl, size_t sc, size_t el, size_t ec, const char *text, size_t tlen,
                    bool has_text) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = na_realloc(l->v, l->cap * sizeof *l->v);
    }
    LspLoc *x = &l->v[l->n++];
    x->path = path;
    x->sl = sl;
    x->sc = sc;
    x->el = el;
    x->ec = ec;
    x->text = has_text ? na_strndup(text, tlen) : NULL;
}

static bool range_of(const J *range, size_t *sl, size_t *sc, size_t *el, size_t *ec) {
    const J *a = range ? j_get(range, "start") : NULL, *b = range ? j_get(range, "end") : NULL;
    if (!a || !b) return false;
    const J *al = j_get(a, "line"), *ac = j_get(a, "character"), *bl = j_get(b, "line"), *bc = j_get(b, "character");
    if (!j_is_int(al) || !j_is_int(ac) || !j_is_int(bl) || !j_is_int(bc) || al->i < 0 || ac->i < 0 || bl->i < 0 || bc->i < 0)
        return false;
    *sl = (size_t)al->i;
    *sc = (size_t)ac->i;
    *el = (size_t)bl->i;
    *ec = (size_t)bc->i;
    return true;
}

static J *text_document_position(LspClient *c, const char *abs_path, const char *text, size_t len, size_t offset) {
    char *uri = NULL;
    if (lsp_client_sync(c, abs_path, text, len, &uri) != 0) return NULL;
    PosEnc enc = ENC_UTF16;
    pos_enc_parse(clangd_encoding(c), &enc);
    size_t line, ch;
    if (lsp_offset_to_position(text, len, offset, enc, &line, &ch) != 0) {
        free(uri);
        na_seterr("invalid position in %s", abs_path);
        return NULL;
    }
    J *td = j_obj(), *pos = j_obj(), *params = j_obj();
    j_set(td, "uri", j_str(uri));
    free(uri);
    j_set(pos, "line", j_int((int64_t)line));
    j_set(pos, "character", j_int((int64_t)ch));
    j_set(params, "textDocument", td);
    j_set(params, "position", pos);
    return params;
}

int lsp_hub_references(LspHub *h, const char *abs_path, const char *text, size_t len, size_t offset,
                       bool include_declaration, LspLocs *out) {
    memset(out, 0, sizeof *out);
    Server *s;
    LspClient *c = client_for(h, abs_path, &s);
    if (!c) return na_fail("no_language_server: %s (%s)", abs_path, s && s->error ? s->error : "no server configured");
    J *params = text_document_position(c, abs_path, text, len, offset);
    if (!params) return -1;
    J *ctx = j_obj();
    j_set(ctx, "includeDeclaration", j_bool(include_declaration));
    j_set(params, "context", ctx);
    J *res = NULL;
    if (clangd_request(c, "textDocument/references", params, &res) != 0) return -1;
    if (res && res->k == J_ARR) {
        for (size_t i = 0; i < res->n; i++) {
            const J *it = res->items[i];
            const J *uri = j_get(it, "uri");
            if (!j_is_str(uri)) uri = j_get(it, "targetUri");
            const J *range = j_get(it, "range");
            if (!range) range = j_get(it, "targetSelectionRange");
            size_t sl, sc, el, ec;
            char *path = j_is_str(uri) ? lsp_uri_to_path(uri->s) : NULL;
            if (!path || !range_of(range, &sl, &sc, &el, &ec)) {
                free(path);
                continue;
            }
            loc_add(out, path, sl, sc, el, ec, NULL, 0, false);
        }
    }
    json_free(res);
    return 0;
}

static void collect_edits(const J *edits, const char *uri, LspLocs *out) {
    char *path = lsp_uri_to_path(uri);
    if (!path || !edits || edits->k != J_ARR) {
        free(path);
        return;
    }
    for (size_t i = 0; i < edits->n; i++) {
        const J *e = edits->items[i], *nt = j_get(e, "newText");
        size_t sl, sc, el, ec;
        if (!j_is_str(nt) || !range_of(j_get(e, "range"), &sl, &sc, &el, &ec)) continue;
        loc_add(out, na_strdup(path), sl, sc, el, ec, nt->s, nt->slen, true);
    }
    free(path);
}

int lsp_hub_rename(LspHub *h, const char *abs_path, const char *text, size_t len, size_t offset,
                   const char *new_name, LspLocs *out) {
    memset(out, 0, sizeof *out);
    Server *s;
    LspClient *c = client_for(h, abs_path, &s);
    if (!c) return na_fail("no_language_server: %s (%s)", abs_path, s && s->error ? s->error : "no server configured");
    J *params = text_document_position(c, abs_path, text, len, offset);
    if (!params) return -1;
    j_set(params, "newName", j_str(new_name));
    J *res = NULL;
    if (clangd_request(c, "textDocument/rename", params, &res) != 0) return -1;
    if (!res || res->k != J_OBJ) {
        json_free(res);
        return na_fail("rename_unsupported: the language server returned no edits");
    }

    const J *changes = j_get(res, "changes"), *docs = j_get(res, "documentChanges");
    if (changes && changes->k == J_OBJ)
        for (size_t i = 0; i < changes->n; i++) collect_edits(changes->items[i], changes->keys[i], out);
    if (docs && docs->k == J_ARR) {
        for (size_t i = 0; i < docs->n; i++) {
            const J *d = docs->items[i], *td = j_get(d, "textDocument");
            if (j_get(d, "kind")) { /* create / rename / delete file operations are not applied */
                lsp_locs_free(out);
                json_free(res);
                return na_fail("rename_unsupported: the server asked for file operations");
            }
            const J *uri = td ? j_get(td, "uri") : NULL;
            if (j_is_str(uri)) collect_edits(j_get(d, "edits"), uri->s, out);
        }
    }
    json_free(res);
    return 0;
}
