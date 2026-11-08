#include "lsp.h"
#include "platform.h"
#include "rpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fputs("usage: nextagent [--root DIR] [--brief] [--no-sync] [--version] <command> [args]\n"
          "  view PATH [--lines A-B | --symbol NAME [--kind K] [--scope S]] [--raw]\n"
          "  replace PATH (--old S|--old-file F | --lines A-B) (--new S|--new-file F) [--all] [--if-rev N]\n"
          "  replace-symbol PATH NAME (--new S|--new-file F) [--kind K] [--scope S] [--if-rev N]\n"
          "  edit [--request FILE]          JSON request {ops:[{path,start,end,text}],if_rev,label}\n"
          "  outline PATH [--nested]\n"
          "  select PATH NAME [--kind K] [--scope S]\n"
          "  undo [--rev N]\n"
          "  redo\n"
          "  history [--limit N]\n"
          "  sync [PATH...]\n"
          "  references NAME [--no-members] [--path P]...\n"
          "  rename NAME NEW_NAME [--no-members] [--allow-existing] [--path P]... [--if-rev N]\n"
          "  add-param FUNCTION PARAM [--default EXPR] [--position N] [--path P]...\n"
          "  add-include TARGET HEADER [--system] [--if-rev N]\n"
          "  find QUERY [--kind K] [--limit N]\n"
          "  search QUERY [--word] [-i] [--context N] [--path SUBSTR] [--limit N]\n"
          "  merge [PATH...] [--ours|--theirs] [--dry-run]   reconcile files changed outside nextagent (3-way merge)\n"
          "  callers NAME [--depth N] [--path P] [--kind K] [--scope S]   who calls it (transitive with depth)\n"
          "  callees NAME [--depth N] [--path P]    what it calls (syntactic, from the workspace index)\n"
          "  context QUERY [--budget TOKENS] [--path SUBSTR]   best code for a question, cut to a token budget\n"
          "  locate QUERY [--path SUBSTR] [--limit N] [--embed N]    ranked hybrid search (BM25 + names + phrase)\n"
          "  patch [FILE|-]                 JSON edits [{path,old,new,all}] applied as one revision\n"
          "  serve [--clangd[=EXE]|--lsp [--no-gate]]        JSON-RPC 2.0, one object per line on stdio\n"
          "  mcp [--clangd[=EXE]|--lsp [--no-gate]]         MCP stdio bridge\n",
          stderr);
}

static bool g_raw; /* view --raw */

static bool g_autosync = true; /* flush committed revisions to disk after each CLI command (--no-sync disables) */
static bool g_brief;           /* --brief: compact text instead of JSON (far fewer tokens for agents) */

static void brief_symbol(const J *s, const char *path) {
    const J *scope = j_get(s, "scope");
    if (path) printf("%s:", path);
    printf("%lld %s %s%s%s\n", (long long)j_get(s, "line")->i, j_get(s, "kind")->s, j_is_str(scope) && scope->s[0] ? scope->s : "",
           j_is_str(scope) && scope->s[0] ? "::" : "", j_get(s, "name")->s);
}

/* Compact rendering of the common results; false = fall back to JSON. */
static bool print_brief(const char *method, const J *res) {
    const J *syms = j_get(res, "symbols"), *refs = j_get(res, "references"), *paths = j_get(res, "paths");
    if (syms && syms->k == J_ARR) {
        for (size_t i = 0; i < syms->n; i++) {
            const J *p = j_get(syms->items[i], "path");
            brief_symbol(syms->items[i], j_is_str(p) ? p->s : NULL);
        }
        return true;
    }
    const J *results = j_get(res, "results");
    if (results && results->k == J_ARR) { /* merge report */
        for (size_t i = 0; i < results->n; i++) {
            const J *o = results->items[i], *conf = j_get(o, "conflicts");
            printf("%s %s\n", j_get(o, "status")->s, j_get(o, "path")->s);
            for (size_t k = 0; conf && k < conf->n; k++) {
                const J *c = conf->items[k];
                printf("  lines %lld-%lld\n<<<<<<< ours\n%s=======\n%s>>>>>>> theirs\n", (long long)j_get(c, "first_line")->i,
                       (long long)j_get(c, "last_line")->i, j_get(c, "ours")->s, j_get(c, "theirs")->s);
            }
        }
        if (results->n == 0) printf("nothing to merge\n");
        return true;
    }
    const J *nodes = j_get(res, "nodes");
    if (nodes && nodes->k == J_ARR) { /* call graph: indent by depth */
        for (size_t i = 0; i < nodes->n; i++) {
            const J *n = nodes->items[i], *sites = j_get(n, "sites"), *amb = j_get(n, "ambiguous");
            printf("%*s%s %s:%lld", (int)(j_get(n, "depth")->i - 1) * 2, "", j_get(n, "name")->s, j_get(n, "path")->s, (long long)j_get(n, "line")->i);
            for (size_t k = 0; sites && k < sites->n; k++) printf("%s%lld", k ? "," : " @", (long long)sites->items[k]->i);
            if (amb && amb->b) printf(" (ambiguous)");
            printf("\n");
        }
        if (j_get(res, "truncated")->b) printf("-- truncated\n");
        return true;
    }
    const J *blocks = j_get(res, "blocks");
    if (blocks && blocks->k == J_ARR) { /* context pack: a header line per block, then its code */
        for (size_t i = 0; i < blocks->n; i++) {
            const J *b = blocks->items[i], *sym = j_get(b, "symbol");
            printf("## %s:%lld-%lld%s%s\n", j_get(b, "path")->s, (long long)j_get(b, "start_line")->i, (long long)j_get(b, "end_line")->i,
                   sym ? " " : "", sym ? sym->s : "");
            fputs(j_get(b, "text")->s, stdout);
            size_t tl = j_get(b, "text")->slen;
            if (tl == 0 || j_get(b, "text")->s[tl - 1] != '\n') putchar('\n');
        }
        printf("-- ~%lld/%lld tokens%s\n", (long long)j_get(res, "tokens")->i, (long long)j_get(res, "budget")->i,
               j_get(res, "truncated")->b ? ", truncated" : "");
        return true;
    }
    const J *matches = j_get(res, "matches");
    if (matches && matches->k == J_ARR) { /* grep style: path:line: text, context as path-line- text */
        for (size_t i = 0; i < matches->n; i++) {
            const J *m = matches->items[i], *b = j_get(m, "before"), *a = j_get(m, "after");
            const char *path = j_get(m, "path")->s;
            for (size_t k = 0; b && k < b->n; k++)
                printf("%s-%lld- %s\n", path, (long long)j_get(b->items[k], "line")->i, j_get(b->items[k], "text")->s);
            printf("%s:%lld: %s\n", path, (long long)j_get(m, "line")->i, j_get(m, "text")->s);
            for (size_t k = 0; a && k < a->n; k++)
                printf("%s-%lld- %s\n", path, (long long)j_get(a->items[k], "line")->i, j_get(a->items[k], "text")->s);
            if (b || a) printf("--\n");
        }
        return true;
    }
    if (refs && refs->k == J_ARR) {
        for (size_t i = 0; i < refs->n; i++)
            printf("%s:%lld:%lld\n", j_get(refs->items[i], "path")->s, (long long)j_get(refs->items[i], "line")->i,
                   (long long)j_get(refs->items[i], "column")->i);
        return true;
    }
    if (paths && paths->k == J_ARR && j_is_int(j_get(res, "rev"))) {
        printf("rev %lld:", (long long)j_get(res, "rev")->i);
        for (size_t i = 0; i < paths->n; i++) printf(" %s", paths->items[i]->s);
        const J *sites = j_get(res, "sites"), *calls = j_get(res, "calls"), *rep = j_get(res, "replacements");
        if (sites && j_is_int(sites)) printf(" (%lld sites)", (long long)sites->i);
        if (calls && j_is_int(calls)) printf(" (%lld calls)", (long long)calls->i);
        if (rep && j_is_int(rep)) printf(" (%lld replaced)", (long long)rep->i);
        printf("\n");
        return true;
    }
    (void)method;
    return false;
}

/* Run one RPC method and print its result as JSON (or compact text with --brief); returns the exit code. */
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
    } else if (res && g_raw && j_is_str(j_get(res, "text"))) {
        const J *t = j_get(res, "text");
        fwrite(t->s, 1, t->slen, stdout); /* --raw: the document text itself */
    } else if (res) {
        const J *paths = j_get(res, "paths");
        if (g_autosync && paths && paths->k == J_ARR && paths->n) { /* the CLI is one command per process: put the revision on disk */
            const char *names[64];
            size_t n = paths->n < 64 ? paths->n : 64;
            for (size_t i = 0; i < n; i++) names[i] = paths->items[i]->s;
            if (store_flush(r->store, names, n, NULL) != 0) {
                fprintf(stderr, "nextagent: committed but not written to disk: %s\n", na_errmsg());
                code = 1;
            }
        }
        if (!g_brief || !print_brief(method, res)) {
            Buf out;
            buf_init(&out);
            json_write(res, &out);
            buf_putc(&out, '\n');
            fwrite(out.p, 1, out.len, stdout);
            buf_free(&out);
        }
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

/* "A-B" or "A" (1-based, inclusive) */
static int parse_range(const char *s, int64_t *a, int64_t *b) {
    char *end = NULL;
    long long x = strtoll(s, &end, 10);
    if (end == s || x < 1) return -1;
    *a = *b = x;
    if (*end == '-') {
        const char *q = end + 1;
        long long y = strtoll(q, &end, 10);
        if (end == q || y < x) return -1;
        *b = y;
    }
    return *end ? -1 : 0;
}

/* Whole file as text (UTF-8, copied); a trailing newline is kept exactly. */
static int read_text(const char *path, char **out) {
    Buf b;
    buf_init(&b);
    if (pf_read_file(path, &b) != 0) {
        buf_free(&b);
        return -1;
    }
    *out = buf_take(&b);
    return 0;
}

#if defined(__clang__) && defined(_WIN32) && !defined(NA_LIBC23)
/* The MSVC CRT opens stdio in text mode (LF -> CRLF, Ctrl-Z = EOF); switch to
 * binary so JSON framing and codepoint offsets are byte-exact. nextc's libc
 * is already binary. */
int _setmode(int, int);
int _fileno(FILE *);
static void stdio_binary(void) {
    _setmode(_fileno(stdin), 0x8000);
    _setmode(_fileno(stdout), 0x8000);
    _setmode(_fileno(stderr), 0x8000);
}
#else
static void stdio_binary(void) {}
#endif

static const char *const N1[] = {"name", NULL};
static const char *const N2[] = {"name", "new_name"};
static const char *const N3[] = {"function", "param"};
static const char *const N4[] = {"target", "header"};
static const char *const N5[] = {"query", NULL};

/* Generic CLI -> RPC mapping for the refactoring commands: positional
 * parameters by name, then known flags. Returns NULL on a usage error. */
static J *cli_params(const char *const *pos_names, int npos, char **args, int nargs) {
    if (nargs < npos) return NULL;
    J *p = j_obj();
    for (int k = 0; k < npos; k++) j_set(p, pos_names[k], j_str(args[k]));
    for (int k = npos; k < nargs; k++) {
        const char *a = args[k];
        int64_t n;
        if (strcmp(a, "--no-members") == 0) j_set(p, "members", j_bool(false));
        else if (strcmp(a, "--allow-existing") == 0) j_set(p, "allow_existing", j_bool(true));
        else if (strcmp(a, "--system") == 0) j_set(p, "system", j_bool(true));
        else if (k + 1 < nargs && strcmp(a, "--default") == 0) j_set(p, "default", j_str(args[++k]));
        else if (k + 1 < nargs && strcmp(a, "--kind") == 0) j_set(p, "kind", j_str(args[++k]));
        else if (k + 1 < nargs && strcmp(a, "--label") == 0) j_set(p, "label", j_str(args[++k]));
        else if (k + 1 < nargs && strcmp(a, "--position") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "position", j_int(n)), k++;
        else if (k + 1 < nargs && strcmp(a, "--if-rev") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "if_rev", j_int(n)), k++;
        else if (k + 1 < nargs && strcmp(a, "--limit") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "limit", j_int(n)), k++;
        else if (k + 1 < nargs && strcmp(a, "--path") == 0) {
            const J *ex = j_get(p, "paths");
            if (!ex) j_set(p, "paths", j_arr());
            j_push((J *)j_get(p, "paths"), j_str(args[++k]));
        } else {
            json_free(p);
            return NULL;
        }
    }
    return p;
}

int main(int argc, char **argv) {
    stdio_binary();
    if (getenv("NEXTAGENT_BRIEF")) g_brief = true;
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
        } else if (strcmp(argv[i], "--brief") == 0) {
            g_brief = true;
        } else if (strcmp(argv[i], "--no-sync") == 0) {
            g_autosync = false;
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
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "target", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            int64_t a, b;
            if (strcmp(args[k], "--raw") == 0) g_raw = true;
            else if (k + 1 < nargs && strcmp(args[k], "--symbol") == 0) j_set(p, "symbol", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--kind") == 0) j_set(p, "kind", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--scope") == 0) j_set(p, "scope", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--lines") == 0 && parse_range(args[k + 1], &a, &b) == 0) {
                j_set(p, "start_line", j_int(a));
                j_set(p, "end_line", j_int(b));
                k++;
            } else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "view", p);
    } else if (strcmp(cmd, "replace") == 0 || strcmp(cmd, "replace-symbol") == 0) {
        bool sym = cmd[7] != 0;
        int npos = sym ? 2 : 1;
        if (nargs < npos) goto bad;
        J *p = j_obj();
        j_set(p, "target", j_str(args[0]));
        if (sym) j_set(p, "name", j_str(args[1]));
        const char *old = NULL, *neu = NULL;
        char *old_buf = NULL, *new_buf = NULL;
        bool ok = true, by_lines = false;
        int64_t ra = 0, rb = 0;
        for (int k = npos; k < nargs && ok; k++) {
            int64_t n;
            const char *a = args[k];
            if (k + 1 < nargs && !sym && strcmp(a, "--old") == 0) old = args[++k];
            else if (k + 1 < nargs && !sym && strcmp(a, "--old-file") == 0) ok = read_text(args[++k], &old_buf) == 0, old = old_buf;
            else if (k + 1 < nargs && strcmp(a, "--new") == 0) neu = args[++k];
            else if (k + 1 < nargs && strcmp(a, "--new-file") == 0) ok = read_text(args[++k], &new_buf) == 0, neu = new_buf;
            else if (k + 1 < nargs && !sym && strcmp(a, "--lines") == 0 && parse_range(args[k + 1], &ra, &rb) == 0) {
                j_set(p, "start_line", j_int(ra));
                j_set(p, "end_line", j_int(rb));
                by_lines = true;
                k++;
            } else if (!sym && strcmp(a, "--all") == 0) j_set(p, "all", j_bool(true));
            else if (k + 1 < nargs && sym && strcmp(a, "--kind") == 0) j_set(p, "kind", j_str(args[++k]));
            else if (k + 1 < nargs && sym && strcmp(a, "--scope") == 0) j_set(p, "scope", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(a, "--label") == 0) j_set(p, "label", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(a, "--if-rev") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "if_rev", j_int(n)), k++;
            else ok = false;
        }
        if (!ok || !neu || (!sym && !old && !by_lines)) {
            free(old_buf);
            free(new_buf);
            json_free(p);
            goto bad;
        }
        if (!sym && old) j_set(p, "old", j_str(old));
        j_set(p, sym ? "replacement" : "new", j_str(neu));
        if (sym && !j_get(p, "if_rev")) j_set(p, "if_rev", j_int(store_head(store)));
        free(old_buf);
        free(new_buf);
        code = run_method(&rpc, sym ? "replace_symbol" : "replace_text", p);
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
    } else if (strcmp(cmd, "references") == 0 || strcmp(cmd, "rename") == 0 || strcmp(cmd, "add-param") == 0 ||
               strcmp(cmd, "add-include") == 0 || strcmp(cmd, "find") == 0) {
        J *p;
        const char *method;
        if (strcmp(cmd, "references") == 0) p = cli_params(N1, 1, args, nargs), method = "references";
        else if (strcmp(cmd, "rename") == 0) p = cli_params(N2, 2, args, nargs), method = "rename";
        else if (strcmp(cmd, "add-param") == 0) p = cli_params(N3, 2, args, nargs), method = "add_param";
        else if (strcmp(cmd, "add-include") == 0) p = cli_params(N4, 2, args, nargs), method = "add_include";
        else p = cli_params(N5, 1, args, nargs), method = "find_symbols";
        if (!p) goto bad;
        code = run_method(&rpc, method, p);
    } else if (strcmp(cmd, "search") == 0) {
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "query", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            int64_t n;
            if (strcmp(args[k], "--word") == 0) j_set(p, "word", j_bool(true));
            else if (strcmp(args[k], "-i") == 0 || strcmp(args[k], "--ignore-case") == 0) j_set(p, "ignore_case", j_bool(true));
            else if (k + 1 < nargs && strcmp(args[k], "--path") == 0) j_set(p, "path", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--context") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "context", j_int(n)), k++;
            else if (k + 1 < nargs && strcmp(args[k], "--limit") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "limit", j_int(n)), k++;
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "search", p);
    } else if (strcmp(cmd, "merge") == 0) {
        J *p = j_obj(), *paths = j_arr();
        for (int k = 0; k < nargs; k++) {
            if (strcmp(args[k], "--ours") == 0) j_set(p, "strategy", j_str("ours"));
            else if (strcmp(args[k], "--theirs") == 0) j_set(p, "strategy", j_str("theirs"));
            else if (strcmp(args[k], "--dry-run") == 0) j_set(p, "dry_run", j_bool(true));
            else if (args[k][0] != '-') j_push(paths, j_str(args[k]));
            else {
                json_free(p);
                json_free(paths);
                goto bad;
            }
        }
        if (paths->n) j_set(p, "paths", paths);
        else json_free(paths);
        code = run_method(&rpc, "merge", p);
    } else if (strcmp(cmd, "callers") == 0 || strcmp(cmd, "callees") == 0) {
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "name", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            int64_t n;
            if (k + 1 < nargs && strcmp(args[k], "--path") == 0) j_set(p, "path", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--kind") == 0) j_set(p, "kind", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--scope") == 0) j_set(p, "scope", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--depth") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "depth", j_int(n)), k++;
            else if (k + 1 < nargs && strcmp(args[k], "--limit") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "limit", j_int(n)), k++;
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, cmd, p);
    } else if (strcmp(cmd, "context") == 0) {
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "query", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            int64_t n;
            if (k + 1 < nargs && strcmp(args[k], "--path") == 0) j_set(p, "path", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--budget") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "budget", j_int(n)), k++;
            else if (k + 1 < nargs && strcmp(args[k], "--embed") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "embed_budget", j_int(n)), k++;
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "context", p);
    } else if (strcmp(cmd, "locate") == 0) {
        if (nargs < 1) goto bad;
        J *p = j_obj();
        j_set(p, "query", j_str(args[0]));
        for (int k = 1; k < nargs; k++) {
            int64_t n;
            if (k + 1 < nargs && strcmp(args[k], "--path") == 0) j_set(p, "path", j_str(args[++k]));
            else if (k + 1 < nargs && strcmp(args[k], "--limit") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "limit", j_int(n)), k++;
            else if (k + 1 < nargs && strcmp(args[k], "--embed") == 0 && parse_int(args[k + 1], &n) == 0) j_set(p, "embed_budget", j_int(n)), k++;
            else {
                json_free(p);
                goto bad;
            }
        }
        code = run_method(&rpc, "hybrid_search", p);
    } else if (strcmp(cmd, "patch") == 0) {
        /* patch [FILE|-]: JSON {"edits":[{path,old,new,all}],"if_rev":N,"label":S} or just the edits array */
        Buf data;
        buf_init(&data);
        if (nargs >= 1 && strcmp(args[0], "-") != 0) {
            if (pf_read_file(args[0], &data) != 0) {
                fprintf(stderr, "nextagent: cannot read %s\n", args[0]);
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
        if (req && req->k == J_ARR) { /* bare array of edits */
            J *w = j_obj();
            j_set(w, "edits", req);
            req = w;
        }
        if (!req || req->k != J_OBJ) {
            fprintf(stderr, "nextagent: %s\n", req ? "patch must be a JSON object or array" : na_errmsg());
            json_free(req);
            code = 1;
            goto done;
        }
        code = run_method(&rpc, "patch", req);
    } else if (strcmp(cmd, "sync") == 0) {
        J *p = j_obj();
        if (nargs > 0) {
            J *paths = j_arr();
            for (int k = 0; k < nargs; k++) j_push(paths, j_str(args[k]));
            j_set(p, "paths", paths);
        }
        code = run_method(&rpc, "sync", p);
    } else if (strcmp(cmd, "serve") == 0 || strcmp(cmd, "mcp") == 0) {
        const char *clangd = NULL, *lsp_clangd = NULL;
        bool use_hub = false, no_gate = false;
        for (int k = 0; k < nargs; k++) {
            if (strcmp(args[k], "--clangd") == 0) clangd = "clangd";
            else if (strncmp(args[k], "--clangd=", 9) == 0) clangd = args[k] + 9;
            else if (strcmp(args[k], "--no-gate") == 0) no_gate = true;
            else if (strcmp(args[k], "--lsp") == 0) use_hub = true;
            else if (strncmp(args[k], "--lsp=", 6) == 0) use_hub = true, lsp_clangd = args[k] + 6;
            else goto bad;
        }
        if (use_hub && clangd) goto bad;
        LspHub *hub = NULL;
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
        if (use_hub) {
            hub = lsp_hub_new(store_root(store), lsp_clangd && *lsp_clangd ? lsp_clangd : NULL, 20000);
            if (!no_gate) { /* --no-gate: language servers for navigation/refactoring only, no diagnostic gate */
                gate = gate_new(store_root(store), lsp_hub_ops(hub), store, 2);
                rpc.validator = gate_validator(gate);
                rpc.has_validator = true;
            }
            rpc.hub = hub;
        }
        rpc.session = "jsonrpc";
        if (rpc_serve(&rpc, stdin, stdout, strcmp(cmd, "mcp") == 0) != 0) {
            fprintf(stderr, "nextagent: %s\n", na_errmsg());
            code = 1;
        }
        gate_free(gate);
        lsp_hub_free(hub);
        clangd_close(client);
    } else {
        goto bad;
    }
done:
    rpc_release(&rpc);
    store_close(store);
    return code;
bad:
    usage();
    rpc_release(&rpc);
    store_close(store);
    return 2;
}
