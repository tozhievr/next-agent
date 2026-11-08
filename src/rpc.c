#include "rpc.h"
#include "c23.h"
#include "platform.h"
#include "refactor.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LINE (32u * 1024u * 1024u)

static J *j_clone(const J *v) {
    char *d = json_dump(v);
    J *c = json_parse(d, strlen(d));
    free(d);
    return c ? c : j_null();
}

J *edit_result_json(const EditResult *res) {
    J *o = j_obj(), *paths = j_arr();
    for (size_t i = 0; i < res->paths.n; i++) j_push(paths, j_str(res->paths.v[i]));
    j_set(o, "rev", j_int(res->rev));
    j_set(o, "paths", paths);
    return o;
}

/* ------------------------------------------------------ param validation */
static int need_str(const J *p, const char *key, const char **out) {
    const J *v = j_get(p, key);
    if (!v) return na_fail("missing parameter: %s", key);
    if (!j_is_str(v)) return na_fail("invalid parameter: %s must be a string", key);
    *out = v->s;
    return 0;
}

static int opt_str(const J *p, const char *key, const char **out) {
    const J *v = j_get(p, key);
    *out = NULL;
    if (!v || v->k == J_NULL) return 0;
    if (!j_is_str(v)) return na_fail("invalid parameter: %s must be a string", key);
    *out = v->s;
    return 0;
}

static int opt_int(const J *p, const char *key, int64_t dflt, int64_t *out) {
    const J *v = j_get(p, key);
    *out = dflt;
    if (!v || v->k == J_NULL) return 0;
    if (!j_is_int(v)) return na_fail("invalid parameter: %s must be an integer", key);
    *out = v->i;
    return 0;
}

static int opt_bool(const J *p, const char *key, bool dflt, bool *out) {
    const J *v = j_get(p, key);
    *out = dflt;
    if (!v || v->k == J_NULL) return 0;
    if (v->k != J_BOOL) return na_fail("invalid parameter: %s must be a boolean", key);
    *out = v->b;
    return 0;
}

static int need_int(const J *p, const char *key, int64_t *out) {
    if (!j_get(p, key)) return na_fail("missing parameter: %s", key);
    return opt_int(p, key, 0, out);
}

/* ops -> per-path change sets */
typedef struct {
    char **names;
    ChangeSet *sets;
    size_t n;
} Batch;

static void batch_free(Batch *b) {
    for (size_t i = 0; i < b->n; i++) {
        free(b->names[i]);
        cs_free(&b->sets[i]);
    }
    free(b->names);
    free(b->sets);
    memset(b, 0, sizeof *b);
}

static int batch_from_ops(const J *ops, Batch *b) {
    memset(b, 0, sizeof *b);
    if (!ops) return na_fail("missing parameter: ops");
    if (ops->k != J_ARR) return na_fail("invalid parameter: ops must be an array");
    if (ops->n == 0) return na_fail("invalid parameter: ops must not be empty");
    b->names = na_alloc(ops->n * sizeof *b->names);
    b->sets = na_alloc(ops->n * sizeof *b->sets);
    for (size_t i = 0; i < ops->n; i++) {
        const J *op = ops->items[i];
        const char *path = NULL, *text = NULL;
        int64_t start = 0, end = 0;
        if (!op || op->k != J_OBJ) {
            batch_free(b);
            return na_fail("invalid parameter: ops[%zu] must be an object", i);
        }
        if (need_str(op, "path", &path) || need_int(op, "start", &start) || need_int(op, "end", &end) ||
            need_str(op, "text", &text)) {
            batch_free(b);
            return -1;
        }
        if (start < 0 || end < start) {
            batch_free(b);
            return na_fail("invalid parameter: ops[%zu] has an invalid range", i);
        }
        size_t k = 0;
        while (k < b->n && strcmp(b->names[k], path) != 0) k++;
        if (k == b->n) {
            b->names[b->n] = na_strdup(path);
            cs_init(&b->sets[b->n]);
            b->n++;
        }
        cs_add(&b->sets[k], (size_t)start, (size_t)end, text, strlen(text));
    }
    for (size_t i = 0; i < b->n; i++) {
        cs_sort(&b->sets[i]);
        if (cs_validate(&b->sets[i]) != 0) {
            batch_free(b);
            return -1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- methods */
static const Validator *validator_of(Rpc *r) { return r->has_validator ? &r->validator : NULL; }

static int m_view(Rpc *r, const J *p, J **out) {
    const char *target;
    if (need_str(p, "target", &target)) return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    J *o = j_obj();
    j_set(o, "rev", j_int(s->rev));
    j_set(o, "path", j_str(target));
    j_set(o, "text", j_strn(s->text, s->len));
    snap_free(s);
    *out = o;
    return 0;
}

static int m_outline(Rpc *r, const J *p, J **out) {
    const char *target;
    bool nested;
    if (need_str(p, "target", &target) || opt_bool(p, "nested", false, &nested)) return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    SymbolList list;
    int rc = c23_outline(s, nested, &list);
    if (rc == 0) {
        J *arr = j_arr();
        for (size_t i = 0; i < list.n; i++) j_push(arr, symbol_to_json(&list.v[i]));
        J *o = j_obj();
        j_set(o, "symbols", arr);
        *out = o;
        symbols_free(&list);
    }
    snap_free(s);
    return rc;
}

static int m_select(Rpc *r, const J *p, J **out) {
    const char *target, *name, *kind, *scope;
    if (need_str(p, "target", &target) || need_str(p, "name", &name) || opt_str(p, "kind", &kind) ||
        opt_str(p, "scope", &scope))
        return -1;
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    Symbol sym;
    int rc = c23_select(s, name, kind, scope, &sym);
    if (rc == 0) {
        *out = symbol_to_json(&sym);
        free(sym.name);
        free(sym.kind);
        free(sym.scope);
    }
    snap_free(s);
    return rc;
}

static int m_replace_symbol(Rpc *r, const J *p, J **out) {
    const char *target, *name, *replacement, *kind, *scope, *label;
    int64_t expected;
    if (need_str(p, "target", &target) || need_str(p, "name", &name) || need_str(p, "replacement", &replacement) ||
        need_int(p, "if_rev", &expected) || opt_str(p, "kind", &kind) || opt_str(p, "scope", &scope) ||
        opt_str(p, "label", &label))
        return -1;
    if (expected != store_head(r->store))
        return na_fail("stale_rev: expected %lld, current %lld", (long long)expected,
                       (long long)store_head(r->store));
    Snapshot *s;
    if (store_view(r->store, target, &s) != 0) return -1;
    ChangeSet cs;
    int rc = c23_replace_symbol(s, name, replacement, strlen(replacement), kind, scope, &cs);
    snap_free(s);
    if (rc != 0) return -1;
    EditResult res;
    const char *names[1] = {target};
    rc = store_edit(r->store, names, &cs, 1, expected, label ? label : "replace_symbol", r->session ? r->session : "jsonrpc",
                    validator_of(r), &res);
    cs_free(&cs);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_edit(Rpc *r, const J *p, J **out) {
    const char *label;
    int64_t if_rev;
    if (opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev)) return -1;
    Batch b;
    if (batch_from_ops(j_get(p, "ops"), &b) != 0) return -1;
    EditResult res;
    int rc = store_edit(r->store, (const char *const *)b.names, b.sets, b.n, if_rev, label ? label : "edit",
                        r->session ? r->session : "jsonrpc", validator_of(r), &res);
    batch_free(&b);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_history(Rpc *r, const J *p, J **out) {
    const char *op;
    if (opt_str(p, "op", &op)) return -1;
    if (!op) op = "log";
    if (strcmp(op, "log") == 0) {
        int64_t n;
        if (opt_int(p, "n", 20, &n)) return -1;
        if (n < 0 || n > 10000) return na_fail("invalid parameter: n out of range");
        *out = store_history(r->store, (int)n);
        return *out ? 0 : -1;
    }
    EditResult res;
    int rc;
    if (strcmp(op, "undo") == 0) {
        int64_t rev;
        if (opt_int(p, "rev", 0, &rev)) return -1;
        if (rev < 0) return na_fail("invalid parameter: rev must be non-negative");
        rc = store_undo(r->store, rev, validator_of(r), &res);
    } else if (strcmp(op, "redo") == 0) {
        rc = store_redo(r->store, validator_of(r), &res);
    } else {
        return na_fail("unsupported history operation");
    }
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    edit_result_free(&res);
    return 0;
}

static int m_sync(Rpc *r, const J *p, J **out) {
    const char *op;
    if (opt_str(p, "op", &op)) return -1;
    if (op && strcmp(op, "flush") != 0) return na_fail("unsupported sync operation");
    const J *paths = j_get(p, "paths");
    char **names = NULL;
    size_t n = 0;
    if (paths && paths->k != J_NULL) {
        if (paths->k != J_ARR) return na_fail("invalid parameter: paths must be an array");
        names = na_alloc((paths->n + 1) * sizeof *names);
        for (size_t i = 0; i < paths->n; i++) {
            if (!j_is_str(paths->items[i])) {
                free(names);
                return na_fail("invalid parameter: paths must contain strings");
            }
            names[n++] = paths->items[i]->s;
        }
    }
    StrList written;
    sl_init(&written);
    int rc = store_flush(r->store, (const char *const *)names, n, &written);
    free(names);
    if (rc != 0) {
        sl_free(&written);
        return -1;
    }
    J *arr = j_arr(), *o = j_obj();
    for (size_t i = 0; i < written.n; i++) j_push(arr, j_str(written.v[i]));
    sl_free(&written);
    j_set(o, "ok", j_bool(true));
    j_set(o, "written", arr);
    *out = o;
    return 0;
}

static int m_open(Rpc *r, const J *p, J **out) {
    (void)p;
    J *o = j_obj();
    j_set(o, "name", j_str("NextAgent"));
    j_set(o, "version", j_str(NA_VERSION));
    j_set(o, "rev", j_int(store_head(r->store)));
    *out = o;
    return 0;
}

/* ------------------------------------------------------------ refactorings */
static int opt_paths(const J *p, const char *key, StrList *out) {
    sl_init(out);
    const J *v = j_get(p, key);
    if (!v || v->k == J_NULL) return 0;
    if (v->k != J_ARR) return na_fail("invalid parameter: %s must be an array", key);
    for (size_t i = 0; i < v->n; i++) {
        if (!j_is_str(v->items[i])) {
            sl_free(out);
            return na_fail("invalid parameter: %s must contain strings", key);
        }
        sl_push(out, v->items[i]->s);
    }
    return 0;
}

static const char *session_of(Rpc *r) { return r->session ? r->session : "jsonrpc"; }

static int m_references(Rpc *r, const J *p, J **out) {
    const char *name;
    bool members;
    StrList only;
    if (need_str(p, "name", &name) || opt_bool(p, "members", true, &members) || opt_paths(p, "paths", &only)) return -1;
    int rc = rf_references(r->store, name, &only, members, out);
    sl_free(&only);
    return rc;
}

static int m_rename(Rpc *r, const J *p, J **out) {
    const char *name, *new_name, *label;
    bool members, allow;
    int64_t if_rev;
    StrList only;
    if (need_str(p, "name", &name) || need_str(p, "new_name", &new_name) || opt_str(p, "label", &label) ||
        opt_bool(p, "members", true, &members) || opt_bool(p, "allow_existing", false, &allow) ||
        opt_int(p, "if_rev", STORE_NO_REV, &if_rev) || opt_paths(p, "paths", &only))
        return -1;
    EditResult res;
    RfCounts c;
    int rc = rf_rename(r->store, name, new_name, &only, members, allow, if_rev, label, session_of(r), validator_of(r), &res, &c);
    sl_free(&only);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "sites", j_int((int64_t)c.sites));
    edit_result_free(&res);
    return 0;
}

static int m_add_param(Rpc *r, const J *p, J **out) {
    const char *function, *param, *def, *label;
    int64_t pos, if_rev;
    StrList only;
    if (need_str(p, "function", &function) || need_str(p, "param", &param) || opt_str(p, "default", &def) ||
        opt_int(p, "position", -1, &pos) || opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev) ||
        opt_paths(p, "paths", &only))
        return -1;
    EditResult res;
    RfCounts c;
    int rc = rf_add_param(r->store, function, param, def, pos, &only, if_rev, label, session_of(r), validator_of(r), &res, &c);
    sl_free(&only);
    if (rc != 0) return -1;
    *out = edit_result_json(&res);
    j_set(*out, "declarations", j_int((int64_t)c.sites));
    j_set(*out, "calls", j_int((int64_t)c.calls));
    edit_result_free(&res);
    return 0;
}

static int m_add_include(Rpc *r, const J *p, J **out) {
    const char *target, *header, *label;
    bool system;
    int64_t if_rev;
    if (need_str(p, "target", &target) || need_str(p, "header", &header) || opt_bool(p, "system", false, &system) ||
        opt_str(p, "label", &label) || opt_int(p, "if_rev", STORE_NO_REV, &if_rev))
        return -1;
    EditResult res;
    bool changed;
    if (rf_add_include(r->store, target, header, system, if_rev, label, session_of(r), validator_of(r), &res, &changed) != 0)
        return -1;
    *out = edit_result_json(&res);
    j_set(*out, "changed", j_bool(changed));
    edit_result_free(&res);
    return 0;
}

static int m_find_symbols(Rpc *r, const J *p, J **out) {
    const char *query, *kind;
    int64_t limit;
    if (need_str(p, "query", &query) || opt_str(p, "kind", &kind) || opt_int(p, "limit", 100, &limit)) return -1;
    if (limit < 0) return na_fail("invalid parameter: limit must be non-negative");
    return rf_find_symbols(r->store, query, kind, (size_t)limit, out);
}

typedef struct {
    const char *name;
    int (*fn)(Rpc *, const J *, J **);
} Method;

static const Method METHODS[] = {
    {"open", m_open},       {"view", m_view},   {"outline", m_outline}, {"select", m_select},
    {"replace_symbol", m_replace_symbol},       {"edit", m_edit},      {"history", m_history},
    {"sync", m_sync},       {"references", m_references}, {"rename", m_rename},
    {"add_param", m_add_param}, {"add_include", m_add_include}, {"find_symbols", m_find_symbols},
};

/* 0 ok, -1 failure (na_errmsg), -2 unknown method */
static int call_method(Rpc *r, const char *method, const J *params, J **out) {
    *out = NULL;
    for (size_t i = 0; i < sizeof METHODS / sizeof METHODS[0]; i++)
        if (strcmp(METHODS[i].name, method) == 0) return METHODS[i].fn(r, params, out);
    return -2;
}

/* ------------------------------------------------------------- JSON-RPC */
static J *make_error(const J *id, int code, const char *message) {
    J *o = j_obj(), *e = j_obj();
    j_set(o, "jsonrpc", j_str("2.0"));
    j_set(o, "id", id ? j_clone(id) : j_null());
    j_set(e, "code", j_int(code));
    j_set(e, "message", j_str(message));
    j_set(o, "error", e);
    return o;
}

static J *make_result(const J *id, J *result) {
    J *o = j_obj();
    j_set(o, "jsonrpc", j_str("2.0"));
    j_set(o, "id", j_clone(id));
    j_set(o, "result", result ? result : j_null());
    return o;
}

static bool valid_id(const J *id) { return id->k == J_NULL || id->k == J_INT || id->k == J_STR; }

J *rpc_dispatch(Rpc *r, const J *req) {
    if (!req || req->k != J_OBJ) return make_error(NULL, -32600, "Invalid Request");
    const J *id = j_get(req, "id"), *method = j_get(req, "method"), *jv = j_get(req, "jsonrpc");
    if (!jv || !j_is_str(jv) || strcmp(jv->s, "2.0") != 0 || !j_is_str(method) || (id && !valid_id(id)))
        return make_error(id && valid_id(id) ? id : NULL, -32600, "Invalid Request");
    const J *params = j_get(req, "params");
    J *empty = NULL;
    if (!params) params = empty = j_obj();
    J *reply = NULL;
    if (params->k != J_OBJ) {
        if (id) reply = make_error(id, -32602, "Invalid params");
        json_free(empty);
        return reply;
    }
    J *result = NULL;
    int rc = call_method(r, method->s, params, &result);
    if (rc == -2) {
        if (id) reply = make_error(id, -32601, "Method not found");
    } else if (rc != 0) {
        if (id) reply = make_error(id, -32602, na_errmsg());
    } else if (id) {
        reply = make_result(id, result);
        result = NULL;
    }
    json_free(result);
    json_free(empty);
    return reply;
}

/* ------------------------------------------------------------------ MCP */
static J *tool_def(const char *name, const char *desc, const char *schema_json) {
    J *t = j_obj();
    j_set(t, "name", j_str(name));
    j_set(t, "description", j_str(desc));
    J *schema = json_parse(schema_json, strlen(schema_json));
    j_set(t, "inputSchema", schema ? schema : j_obj());
    return t;
}

static J *tools_list(void) {
    J *arr = j_arr();
    j_push(arr, tool_def("view", "Read a document at the current revision.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"}},\"required\":[\"target\"]}"));
    j_push(arr, tool_def("outline", "List C23 top-level (or nested) symbols.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"nested\":{\"type\":\"boolean\"}},\"required\":[\"target\"]}"));
    j_push(arr, tool_def("select", "Locate one symbol by name, kind and scope.",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"}},\"required\":[\"target\",\"name\"]}"));
    j_push(arr, tool_def("replace_symbol", "Atomically replace one symbol (requires if_rev).",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"replacement\":{\"type\":\"string\"},\"if_rev\":{\"type\":\"integer\"},\"kind\":{\"type\":\"string\"},\"scope\":{\"type\":\"string\"},\"label\":{\"type\":\"string\"}},\"required\":[\"target\",\"name\",\"replacement\",\"if_rev\"]}"));
    j_push(arr, tool_def("edit", "Apply codepoint-range edits across files in one atomic revision.",
                         "{\"type\":\"object\",\"properties\":{\"ops\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"start\":{\"type\":\"integer\"},\"end\":{\"type\":\"integer\"},\"text\":{\"type\":\"string\"}},\"required\":[\"path\",\"start\",\"end\",\"text\"]}},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"ops\"]}"));
    j_push(arr, tool_def("history", "Revision log, undo (optionally of a given rev) or redo.",
                         "{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\",\"enum\":[\"log\",\"undo\",\"redo\"]},\"n\":{\"type\":\"integer\"},\"rev\":{\"type\":\"integer\"}}}"));
    j_push(arr, tool_def("sync", "Flush committed revisions to disk.",
                         "{\"type\":\"object\",\"properties\":{\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}}}"));
    j_push(arr, tool_def("references", "List identifier occurrences of a name across the workspace (syntactic).",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"members\":{\"type\":\"boolean\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},\"required\":[\"name\"]}"));
    j_push(arr, tool_def("rename", "Rename an identifier across the workspace in one atomic revision (syntactic).",
                         "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},\"new_name\":{\"type\":\"string\"},\"if_rev\":{\"type\":\"integer\"},\"members\":{\"type\":\"boolean\"},\"allow_existing\":{\"type\":\"boolean\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"label\":{\"type\":\"string\"}},\"required\":[\"name\",\"new_name\"]}"));
    j_push(arr, tool_def("add_param", "Add a parameter to a C function and an argument to every call site.",
                         "{\"type\":\"object\",\"properties\":{\"function\":{\"type\":\"string\"},\"param\":{\"type\":\"string\"},\"default\":{\"type\":\"string\"},\"position\":{\"type\":\"integer\"},\"if_rev\":{\"type\":\"integer\"},\"paths\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"label\":{\"type\":\"string\"}},\"required\":[\"function\",\"param\"]}"));
    j_push(arr, tool_def("add_include", "Insert an #include after the last include (no-op if present).",
                         "{\"type\":\"object\",\"properties\":{\"target\":{\"type\":\"string\"},\"header\":{\"type\":\"string\"},\"system\":{\"type\":\"boolean\"},\"if_rev\":{\"type\":\"integer\"},\"label\":{\"type\":\"string\"}},\"required\":[\"target\",\"header\"]}"));
    j_push(arr, tool_def("find_symbols", "Search top-level symbols across the workspace by name substring.",
                         "{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"kind\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}},\"required\":[\"query\"]}"));
    return arr;
}

J *rpc_mcp_dispatch(Rpc *r, const J *req) {
    if (!req || req->k != J_OBJ) return make_error(NULL, -32600, "Invalid Request");
    const J *id = j_get(req, "id"), *method = j_get(req, "method");
    if (!j_is_str(method) || (id && !valid_id(id))) return make_error(NULL, -32600, "Invalid Request");
    if (!id) return NULL; /* notifications (notifications/initialized, ...) */
    const char *m = method->s;
    if (strcmp(m, "initialize") == 0) {
        J *res = j_obj(), *caps = j_obj(), *tools = j_obj(), *info = j_obj();
        j_set(caps, "tools", tools);
        j_set(info, "name", j_str("nextagent"));
        j_set(info, "version", j_str(NA_VERSION));
        const J *params = j_get(req, "params");
        const J *pv = params ? j_get(params, "protocolVersion") : NULL;
        j_set(res, "protocolVersion", j_str(j_is_str(pv) ? pv->s : "2024-11-05"));
        j_set(res, "capabilities", caps);
        j_set(res, "serverInfo", info);
        return make_result(id, res);
    }
    if (strcmp(m, "ping") == 0) return make_result(id, j_obj());
    if (strcmp(m, "tools/list") == 0) {
        J *res = j_obj();
        j_set(res, "tools", tools_list());
        return make_result(id, res);
    }
    if (strcmp(m, "tools/call") == 0) {
        const J *params = j_get(req, "params");
        const J *name = params ? j_get(params, "name") : NULL;
        const J *args = params ? j_get(params, "arguments") : NULL;
        if (!j_is_str(name) || (args && args->k != J_OBJ && args->k != J_NULL))
            return make_error(id, -32602, "Invalid params");
        J *empty = NULL;
        if (!args || args->k == J_NULL) args = empty = j_obj();
        J *result = NULL;
        int rc = (strcmp(name->s, "open") == 0) ? -2 : call_method(r, name->s, args, &result);
        if (rc == -2) {
            json_free(empty);
            return make_error(id, -32602, "Unknown tool");
        }
        Buf text;
        buf_init(&text);
        if (rc == 0) json_write(result, &text);
        else buf_puts(&text, na_errmsg());
        J *content = j_arr(), *item = j_obj(), *res = j_obj();
        j_set(item, "type", j_str("text"));
        j_set(item, "text", j_strn(text.p ? text.p : "", text.len));
        j_push(content, item);
        j_set(res, "content", content);
        j_set(res, "isError", j_bool(rc != 0));
        buf_free(&text);
        json_free(result);
        json_free(empty);
        return make_result(id, res);
    }
    return make_error(id, -32601, "Method not found");
}

/* ----------------------------------------------------------------- serve */
static int read_line(FILE *in, Buf *line) {
    line->len = 0;
    if (line->p) line->p[0] = 0;
    int c;
    bool any = false;
    while ((c = fgetc(in)) != EOF) {
        any = true;
        if (c == '\n') return 1;
        if (line->len >= MAX_LINE) return -1;
        buf_putc(line, (char)c);
    }
    return any ? 1 : 0;
}

int rpc_serve(Rpc *r, FILE *in, FILE *out, bool mcp) {
    Buf line;
    buf_init(&line);
    int rc;
    while ((rc = read_line(in, &line)) > 0) {
        size_t a = 0;
        while (a < line.len && (line.p[a] == ' ' || line.p[a] == '\t' || line.p[a] == '\r')) a++;
        if (a == line.len) continue;
        J *reply;
        J *req = json_parse(line.p, line.len);
        if (!req) reply = make_error(NULL, -32700, "Parse error");
        else reply = mcp ? rpc_mcp_dispatch(r, req) : rpc_dispatch(r, req);
        json_free(req);
        if (reply) {
            Buf o;
            buf_init(&o);
            json_write(reply, &o);
            buf_putc(&o, '\n');
            fwrite(o.p, 1, o.len, out);
            fflush(out);
            buf_free(&o);
            json_free(reply);
        }
    }
    buf_free(&line);
    return rc < 0 ? na_fail("request line too long") : 0;
}
