#include "lsp.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *uri;
    J *report; /* {"version": n, "diagnostics": [...]} */
} DiagSlot;

struct ClangdClient {
    PfProc *proc;
    LspReader reader;
    int timeout_ms;
    char encoding[16];
    int64_t next_id;
    DiagSlot *slots;
    size_t nslots, cap;
};

static int proc_read(void *ctx, void *buf, size_t cap, int timeout_ms) {
    return pf_proc_read((PfProc *)ctx, buf, cap, timeout_ms);
}

char *lsp_path_to_uri(const char *abs_path) {
    static const char hex[] = "0123456789ABCDEF";
    Buf b;
    buf_init(&b);
    char *norm = path_normalize(abs_path);
    buf_puts(&b, "file://");
    if (norm[0] != '/') buf_putc(&b, '/');    for (const unsigned char *p = (const unsigned char *)norm; *p; p++) {
        unsigned char c = *p;
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                    c == '.' || c == '_' || c == '~' || c == '/' || c == ':';
        if (keep) {
            buf_putc(&b, (char)c);
        } else {
            buf_putc(&b, '%');
            buf_putc(&b, hex[c >> 4]);
            buf_putc(&b, hex[c & 15]);
        }
    }
    free(norm);
    return buf_take(&b);
}

static int send_msg(ClangdClient *c, J *msg) {
    Buf frame;
    buf_init(&frame);
    int rc = lsp_frame_message(msg, &frame);
    json_free(msg);
    if (rc == 0 && pf_proc_write(c->proc, frame.p, frame.len) != 0) rc = na_fail("clangd stdin closed");
    buf_free(&frame);
    return rc;
}

static int notify(ClangdClient *c, const char *method, J *params) {
    J *m = j_obj();
    j_set(m, "jsonrpc", j_str("2.0"));
    j_set(m, "method", j_str(method));
    j_set(m, "params", params);
    return send_msg(c, m);
}

static void store_report(ClangdClient *c, const J *params) {
    const J *uri = j_get(params, "uri"), *diags = j_get(params, "diagnostics"), *ver = j_get(params, "version");
    if (!j_is_str(uri) || !diags || diags->k != J_ARR) return;
    J *report = j_obj();
    if (j_is_int(ver)) j_set(report, "version", j_int(ver->i));
    /* deep copy via round trip */
    char *dump = json_dump(diags);
    J *copy = json_parse(dump, strlen(dump));
    free(dump);
    j_set(report, "diagnostics", copy ? copy : j_arr());
    for (size_t i = 0; i < c->nslots; i++) {
        if (strcmp(c->slots[i].uri, uri->s) == 0) {
            json_free(c->slots[i].report);
            c->slots[i].report = report;
            return;
        }
    }
    if (c->nslots == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->slots = na_realloc(c->slots, c->cap * sizeof *c->slots);
    }
    c->slots[c->nslots].uri = na_strdup(uri->s);
    c->slots[c->nslots].report = report;
    c->nslots++;
}

static J *find_report(ClangdClient *c, const char *uri) {
    for (size_t i = 0; i < c->nslots; i++)
        if (strcmp(c->slots[i].uri, uri) == 0) return c->slots[i].report;
    return NULL;
}

static void drop_report(ClangdClient *c, const char *uri) {
    for (size_t i = 0; i < c->nslots; i++) {
        if (strcmp(c->slots[i].uri, uri) != 0) continue;
        free(c->slots[i].uri);
        json_free(c->slots[i].report);
        c->slots[i] = c->slots[--c->nslots];
        return;
    }
}

/* Process one incoming message. `want_id` >= 0: if the matching response
 * arrives it is returned in *response. Returns 1 handled, 0 timeout, -1 error
 * (including EOF). */
static int pump(ClangdClient *c, int timeout_ms, int64_t want_id, J **response) {
    J *msg = NULL;
    int rc = lsp_read_message(&c->reader, timeout_ms, &msg);
    if (rc == 0) return 0;
    if (rc == 2) return na_fail("clangd closed the stream");
    if (rc < 0) return -1;
    const J *method = j_get(msg, "method"), *id = j_get(msg, "id");
    if (j_is_str(method)) {
        if (id) { /* server -> client request: acknowledge with null */
            J *r = j_obj();
            j_set(r, "jsonrpc", j_str("2.0"));
            char *idc = json_dump(id);
            J *idj = json_parse(idc, strlen(idc));
            free(idc);
            j_set(r, "id", idj);
            j_set(r, "result", j_null());
            send_msg(c, r);
        } else if (strcmp(method->s, "textDocument/publishDiagnostics") == 0) {
            const J *params = j_get(msg, "params");
            if (params) store_report(c, params);
        }
        json_free(msg);
        return 1;
    }
    if (id && j_is_int(id) && id->i == want_id && response) {
        *response = msg;
        return 1;
    }
    json_free(msg);
    return 1;
}

int clangd_request(ClangdClient *c, const char *method, J *params, J **result) {
    int64_t id = ++c->next_id;
    J *m = j_obj();
    j_set(m, "jsonrpc", j_str("2.0"));
    j_set(m, "id", j_int(id));
    j_set(m, "method", j_str(method));
    j_set(m, "params", params ? params : j_obj());
    if (send_msg(c, m) != 0) return -1;
    uint64_t deadline = pf_now_ms() + (uint64_t)c->timeout_ms;
    while (true) {
        uint64_t now = pf_now_ms();
        if (now >= deadline) return na_fail("clangd request timed out: %s", method);
        J *resp = NULL;
        int rc = pump(c, (int)(deadline - now), id, &resp);
        if (rc < 0) return -1;
        if (!resp) continue;
        const J *err = j_get(resp, "error");
        if (err) {
            const J *em = j_get(err, "message");
            na_seterr("clangd error: %s", j_is_str(em) ? em->s : "unknown");
            json_free(resp);
            return -1;
        }
        const J *res = j_get(resp, "result");
        if (result) {
            if (res) {
                char *dump = json_dump(res);
                *result = json_parse(dump, strlen(dump));
                free(dump);
            } else {
                *result = j_null();
            }
        }
        json_free(resp);
        return 0;
    }
}

ClangdClient *clangd_start(const char *root, const char *exe, int timeout_ms) {
    char *path = pf_which(exe ? exe : "clangd");
    if (!path) {
        na_seterr("clangd not found");
        return NULL;
    }
    ClangdClient *c = na_alloc(sizeof *c);
    c->timeout_ms = timeout_ms > 0 ? timeout_ms : 20000;
    c->proc = pf_spawn(path, "--log=error --background-index=false --header-insertion=never", root);
    free(path);
    if (!c->proc) {
        free(c);
        return NULL;
    }
    lsp_reader_init(&c->reader, proc_read, c->proc);
    snprintf(c->encoding, sizeof c->encoding, "utf-16");

    char *rooturi = lsp_path_to_uri(root);
    J *params = j_obj();
    j_set(params, "processId", j_null());
    j_set(params, "rootUri", j_str(rooturi));
    free(rooturi);
    J *caps = j_obj(), *general = j_obj(), *encs = j_arr();
    j_push(encs, j_str("utf-8"));
    j_push(encs, j_str("utf-16"));
    j_set(general, "positionEncodings", encs);
    j_set(caps, "general", general);
    J *td = j_obj(), *pd = j_obj();
    j_set(pd, "versionSupport", j_bool(true));
    j_set(td, "publishDiagnostics", pd);
    j_set(caps, "textDocument", td);
    j_set(params, "capabilities", caps);
    J *res = NULL;
    if (clangd_request(c, "initialize", params, &res) != 0) {
        clangd_close(c);
        return NULL;
    }
    const J *capres = j_get(res, "capabilities");
    const J *enc = capres ? j_get(capres, "positionEncoding") : NULL;
    if (j_is_str(enc)) {
        PosEnc tmp;
        if (pos_enc_parse(enc->s, &tmp) != 0 || strlen(enc->s) >= sizeof c->encoding) {
            json_free(res);
            clangd_close(c);
            return NULL;
        }
        snprintf(c->encoding, sizeof c->encoding, "%s", enc->s);
    }
    json_free(res);
    notify(c, "initialized", j_obj());
    return c;
}

void clangd_close(ClangdClient *c) {
    if (!c) return;
    if (c->proc) {
        J *res = NULL;
        int saved = c->timeout_ms;
        c->timeout_ms = 1000;
        if (clangd_request(c, "shutdown", NULL, &res) == 0) {
            json_free(res);
            notify(c, "exit", j_null());
        }
        c->timeout_ms = saved;
        pf_proc_close(c->proc, 2000);
    }
    lsp_reader_free(&c->reader);
    for (size_t i = 0; i < c->nslots; i++) {
        free(c->slots[i].uri);
        json_free(c->slots[i].report);
    }
    free(c->slots);
    free(c);
}

const char *clangd_encoding(const ClangdClient *c) { return c->encoding; }

/* ----------------------------------------------------------- LspOps glue */
static const char *language_of(const char *path) {
    return path_has_suffix_ci(path, ".c") || path_has_suffix_ci(path, ".h") ? "c" : "cpp";
}

static int op_open(void *ctx, const char *abs_path, const char *text, size_t len, int64_t version, char **uri) {
    ClangdClient *c = ctx;
    *uri = lsp_path_to_uri(abs_path);
    drop_report(c, *uri);
    J *doc = j_obj();
    j_set(doc, "uri", j_str(*uri));
    j_set(doc, "languageId", j_str(language_of(abs_path)));
    j_set(doc, "version", j_int(version));
    j_set(doc, "text", j_strn(text, len));
    J *params = j_obj();
    j_set(params, "textDocument", doc);
    int rc = notify(c, "textDocument/didOpen", params);
    if (rc != 0) {
        free(*uri);
        *uri = NULL;
    }
    return rc;
}

static int op_change(void *ctx, const char *uri, const char *text, size_t len, int64_t version) {
    ClangdClient *c = ctx;
    J *doc = j_obj();
    j_set(doc, "uri", j_str(uri));
    j_set(doc, "version", j_int(version));
    J *change = j_obj();
    j_set(change, "text", j_strn(text, len));
    J *changes = j_arr();
    j_push(changes, change);
    J *params = j_obj();
    j_set(params, "textDocument", doc);
    j_set(params, "contentChanges", changes);
    return notify(c, "textDocument/didChange", params);
}

static int op_wait(void *ctx, const char *uri, int64_t version, J **report) {
    ClangdClient *c = ctx;
    uint64_t deadline = pf_now_ms() + (uint64_t)c->timeout_ms;
    while (true) {
        J *r = find_report(c, uri);
        const J *ver = r ? j_get(r, "version") : NULL;
        if (j_is_int(ver) && ver->i >= version) {
            char *dump = json_dump(r);
            *report = json_parse(dump, strlen(dump));
            free(dump);
            return *report ? 0 : -1;
        }
        uint64_t now = pf_now_ms();
        if (now >= deadline) {
            const J *seen = r ? j_get(r, "version") : NULL;
            return na_fail("diagnostics_unavailable_or_stale: %s (wanted version %lld, last seen %lld)", uri,
                           (long long)version, j_is_int(seen) ? (long long)seen->i : -1LL);
        }
        if (pump(c, (int)(deadline - now), -1, NULL) < 0) return -1;
    }
}

static int op_close(void *ctx, const char *uri) {
    ClangdClient *c = ctx;
    drop_report(c, uri);
    J *doc = j_obj();
    j_set(doc, "uri", j_str(uri));
    J *params = j_obj();
    j_set(params, "textDocument", doc);
    return notify(c, "textDocument/didClose", params);
}

LspOps clangd_ops(ClangdClient *c) {
    LspOps o;
    o.open_document = op_open;
    o.change_document = op_change;
    o.wait_diagnostics = op_wait;
    o.close_document = op_close;
    o.ctx = c;
    return o;
}

int clangd_formatting(ClangdClient *c, const char *uri, int tab_size, J **edits) {
    J *doc = j_obj();
    j_set(doc, "uri", j_str(uri));
    J *opts = j_obj();
    j_set(opts, "tabSize", j_int(tab_size));
    j_set(opts, "insertSpaces", j_bool(true));
    J *params = j_obj();
    j_set(params, "textDocument", doc);
    j_set(params, "options", opts);
    return clangd_request(c, "textDocument/formatting", params, edits);
}
