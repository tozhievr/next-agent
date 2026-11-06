#include "lsp.h"
#include "platform.h"
#include "rpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fputs("usage: nextagent [--root DIR] [--version] <command> [args]\n"
          "  view PATH\n"
          "  edit [--request FILE]          JSON request {ops:[{path,start,end,text}],if_rev,label}\n"
          "  outline PATH [--nested]\n"
          "  select PATH NAME [--kind K] [--scope S]\n"
          "  undo [--rev N]\n"
          "  redo\n"
          "  history [--limit N]\n"
          "  sync [PATH...]\n"
          "  serve [--clangd[=EXE]]         JSON-RPC 2.0, one object per line on stdio\n"
          "  mcp [--clangd[=EXE]]           MCP stdio bridge\n",
          stderr);
}

/* Run one RPC method and print its result as JSON; returns the exit code. */
static int run_method(Rpc *r, const char *method, J *params) {
    J *req = j_obj();
    j_set(req, "jsonrpc", j_str("2.0"));
    j_set(req, "id", j_int(1));
    j_set(req, "method", j_str(method));
    j_set(req, "params", params);
    J *reply = rpc_dispatch(r, req);
    json_free(req);
    if (!reply) return 1;
    const J *err = j_get(reply, "error"), *res = j_get(reply, "result");
    int code = 0;
    if (err) {
        const J *msg = j_get(err, "message");
        fprintf(stderr, "nextagent: %s\n", j_is_str(msg) ? msg->s : "error");
        code = 1;
    } else if (res) {
        Buf out;
        buf_init(&out);
        json_write(res, &out);
        buf_putc(&out, '\n');
        fwrite(out.p, 1, out.len, stdout);
        buf_free(&out);
    }
    json_free(reply);
    return code;
}

static int parse_int(const char *s, int64_t *out) {
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (!s[0] || *end) return -1;
    *out = v;
    return 0;
}

int main(int argc, char **argv) {
    const char *root = ".";
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("NextAgent %s\n", NA_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root = argv[++i];
        } else if (strncmp(argv[i], "--root=", 7) == 0) {
            root = argv[i] + 7;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            break;
        }
    }
    if (i >= argc) {
        usage();
        return 2;
    }
    const char *cmd = argv[i++];
    int nargs = argc - i;
    char **args = argv + i;

    Store *store = store_open(root);
    if (!store) {
        fprintf(stderr, "nextagent: %s\n", na_errmsg());
        return 1;
    }
    Rpc rpc;
    memset(&rpc, 0, sizeof rpc);
    rpc.store = store;
    rpc.session = "cli";
    int code = 0;

    if (strcmp(cmd, "view") == 0) {
        if (nargs != 1) goto bad;
        J *p = j_obj();
        j_set(p, "target", j_str(args[0]));
        code = run_method(&rpc, "view", p);
    } else if (strcmp(cmd, "edit") == 0) {
        const char *file = NULL;
        for (int k = 0; k < nargs; k++) {
            if (strcmp(args[k], "--request") == 0 && k + 1 < nargs) file = args[++k];
            else goto bad;
        }
        Buf data;
        buf_init(&data);
        if (file) {
            if (pf_read_file(file, &data) != 0) {
                fprintf(stderr, "nextagent: cannot read %s\n", file);
                code = 1;
                goto done;
            }
        } else {
            char chunk[4096];
            size_t n;
            while ((n = fread(chunk, 1, sizeof chunk, stdin)) > 0) buf_put(&data, chunk, n);
        }
        J *req = json_parse(data.p ? data.p : "", data.len);
        buf_free(&data);
        if (!req || req->k != J_OBJ) {
            fprintf(stderr, "nextagent: %s\n", req ? "request must be a JSON object" : na_errmsg());
            json_free(req);
            code = 1;
            goto done;
        }
        code = run_method(&rpc, "edit", req);
    } else if (strcmp(cmd, "outline") == 0) {
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "target", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            if (strcmp(args[k], "--nested") == 0) j_set(p, "nested", j_bool(true));
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "outline", p);
    } else if (strcmp(cmd, "select") == 0) {
        if (nargs < 2) goto bad;
        J *p = j_obj();
        j_set(p, "target", j_str(args[0]));
        j_set(p, "name", j_str(args[1]));
        for (int k = 2; k < nargs; k++) {
            if (strcmp(args[k], "--kind") == 0 && k + 1 < nargs) j_set(p, "kind", j_str(args[++k]));
            else if (strcmp(args[k], "--scope") == 0 && k + 1 < nargs) j_set(p, "scope", j_str(args[++k]));
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "select", p);
    } else if (strcmp(cmd, "undo") == 0 || strcmp(cmd, "redo") == 0) {
        J *p = j_obj();
        j_set(p, "op", j_str(cmd));
        for (int k = 0; k < nargs; k++) {
            int64_t rev;
            if (strcmp(cmd, "undo") == 0 && strcmp(args[k], "--rev") == 0 && k + 1 < nargs &&
                parse_int(args[k + 1], &rev) == 0) {
                j_set(p, "rev", j_int(rev));
                k++;
            } else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "history", p);
    } else if (strcmp(cmd, "history") == 0) {
        J *p = j_obj();
        j_set(p, "op", j_str("log"));
        for (int k = 0; k < nargs; k++) {
            int64_t n;
            if (strcmp(args[k], "--limit") == 0 && k + 1 < nargs && parse_int(args[k + 1], &n) == 0) {
                j_set(p, "n", j_int(n));
                k++;
            } else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "history", p);
    } else if (strcmp(cmd, "sync") == 0) {
        J *p = j_obj();
        if (nargs > 0) {
            J *paths = j_arr();
            for (int k = 0; k < nargs; k++) j_push(paths, j_str(args[k]));
            j_set(p, "paths", paths);
        }
        code = run_method(&rpc, "sync", p);
    } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "mcp") == 0) {
        const char *clangd = NULL;
        for (int k = 0; k < nargs; k++) {
            if (strcmp(args[k], "--clangd") == 0) clangd = "clangd";
            else if (strncmp(args[k], "--clangd=", 9) == 0) clangd = args[k] + 9;
            else goto bad;
        }
        ClangdClient *client = NULL;
        DiagnosticGate *gate = NULL;
        if (clangd) {
            client = clangd_start(store_root(store), clangd, 20000);
            if (!client) {
                fprintf(stderr, "nextagent: %s\n", na_errmsg());
                code = 1;
                goto done;
            }
            gate = gate_new(store_root(store), clangd_ops(client), store, 2);
            rpc.validator = gate_validator(gate);
            rpc.has_validator = true;
        }
        rpc.session = "jsonrpc";
        if (rpc_serve(&rpc, stdin, stdout, strcmp(cmd, "mcp") == 0) != 0) {
            fprintf(stderr, "nextagent: %s\n", na_errmsg());
            code = 1;
        }
        gate_free(gate);
        clangd_close(client);
    } else {
        goto bad;
    }
done:
    store_close(store);
    return code;
bad:
    usage();
    store_close(store);
    return 2;
}
