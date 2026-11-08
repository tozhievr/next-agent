#include "lsp.h"
#include "platform.h"
#include "rpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char USAGE[] =
    "usage: nextagent [--root DIR] [--brief] [--no-sync] [--version] <command> [args]   (global flags may appear anywhere)\n"
    "  apply [FILE|-] [--label S]     MANY edits in ONE revision from a script, no quoting:\n"
    "                                   @@@ replace PATH [all] / old text / @@@ with / new text\n"
    "                                   @@@ lines PATH A-B / new lines        @@@ symbol PATH NAME / new text\n"
    "                                   @@@ insert PATH before-line N|after-line N|before-symbol NAME|after-symbol NAME / text\n"
    "                                   @@@ create PATH / file text\n"
    "  create PATH (--new S|--new-file F)\n"
    "  sub OLD NEW PATH... [--all]    one-liners (one revision): literal replacement in one or more files\n"
    "  line PATH A[-B] TEXT           replace line(s) A..B with TEXT        del PATH A[-B]    delete line(s)\n"
    "  ins PATH N TEXT                insert TEXT before line N             append PATH TEXT  add after the last line\n"
    "                                 (write @BS@ for a backslash that must survive your shell)\n"
    "  insert PATH (--before-line N|--after-line N|--before-symbol NAME|--after-symbol NAME) (--new S|--new-file F) [--kind K] [--scope S]\n"
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
    "  mcp [--clangd[=EXE]|--lsp [--no-gate]]         MCP stdio bridge\n";

static void usage(void) { fputs(USAGE, stderr); }

/* a wrong command line: say so in one line and show only that command's synopsis (not the whole manual) */
static void usage_for(const char *cmd) {
    size_t cl = strlen(cmd), shown = 0;
    for (const char *p = USAGE; *p;) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > cl + 2 && p[0] == ' ' && p[1] == ' ' && memcmp(p + 2, cmd, cl) == 0 && (p[2 + cl] == ' ' || p[2 + cl] == '\0')) {
            if (!shown) fprintf(stderr, "nextagent: bad arguments for `%s`:\n", cmd);
            fprintf(stderr, "%.*s\n", (int)n, p);
            shown++;
        }
        p += n + (e ? 1 : 0);
    }
    if (!shown) fprintf(stderr, "nextagent: unknown command `%s` (nextagent --help lists them)\n", cmd);
}

static bool g_raw; /* view --raw */
static int flush_with_merge(Store *st, const char *const *names, size_t n);

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
    if (res->k == J_ARR && strcmp(method, "history") == 0) { /* revision log: one line per revision */
        for (size_t i = 0; i < res->n; i++) {
            const J *o = res->items[i], *par = j_get(o, "parent");
            printf("%lld <- %lld %s\n", (long long)j_get(o, "rev")->i, j_is_int(par) ? (long long)par->i : 0LL, j_get(o, "label")->s);
        }
        return true;
    }
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
            if (flush_with_merge(r->store, names, n) != 0) {
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

/* ---- apply: a multi-edit script that needs no quoting ----
 *   @@@ replace PATH [all]            old text
 *   @@@ with                          new text
 *   @@@ lines PATH A-B                new lines (empty body deletes them)
 *   @@@ insert PATH before-line N | after-line N | before-symbol NAME | after-symbol NAME      new lines
 *   @@@ symbol PATH NAME              the new text of the whole symbol
 *   @@@ create PATH                   the new file
 * A body runs to the next `@@@` line. For replace the final newline of each text is dropped (so an
 * inline fragment can be replaced); elsewhere the body keeps its lines. Everything is ONE revision. */
typedef struct {
    char *w[8];
    int n;
} Words8;

static void words8_free(Words8 *w) {
    for (int i = 0; i < w->n; i++) free(w->w[i]);
    w->n = 0;
}

static void split_words(const char *s, size_t n, Words8 *out) {
    out->n = 0;
    for (size_t i = 0; i < n && out->n < 8;) {
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        size_t b = i;
        while (i < n && s[i] != ' ' && s[i] != '\t') i++;
        if (i > b) out->w[out->n++] = na_strndup(s + b, i - b);
    }
}

static void drop_last_newline(Buf *b) {
    if (b->len > 0 && b->p[b->len - 1] == '\n') b->len--;
}

static int apply_finish(J *edits, const Words8 *h, Buf *body, Buf *neu, bool have_with, size_t at, char *err, size_t cap) {
    const char *op = h->w[0];
    J *e = j_obj();
    if (strcmp(op, "replace") == 0) {
        if (h->n < 2 || !have_with) {
            snprintf(err, cap, "line %zu: replace needs PATH and a `@@@ with` line", at);
            json_free(e);
            return -1;
        }
        drop_last_newline(body);
        drop_last_newline(neu);
        j_set(e, "path", j_str(h->w[1]));
        j_set(e, "old", j_strn(body->p ? body->p : "", body->len));
        j_set(e, "new", j_strn(neu->p ? neu->p : "", neu->len));
        if (h->n > 2 && strcmp(h->w[2], "all") == 0) j_set(e, "all", j_bool(true));
    } else if (strcmp(op, "lines") == 0 && h->n >= 3) {
        int64_t a, b;
        if (parse_range(h->w[2], &a, &b) != 0) {
            snprintf(err, cap, "line %zu: lines needs A-B", at);
            json_free(e);
            return -1;
        }
        j_set(e, "path", j_str(h->w[1]));
        j_set(e, "start_line", j_int(a));
        j_set(e, "end_line", j_int(b));
        j_set(e, "new", j_strn(body->p ? body->p : "", body->len));
    } else if (strcmp(op, "insert") == 0 && h->n >= 4) {
        const char *mode = h->w[2];
        int64_t n;
        j_set(e, "path", j_str(h->w[1]));
        if (strcmp(mode, "before-line") == 0 && parse_int(h->w[3], &n) == 0) j_set(e, "before_line", j_int(n));
        else if (strcmp(mode, "after-line") == 0 && parse_int(h->w[3], &n) == 0) j_set(e, "after_line", j_int(n));
        else if (strcmp(mode, "before-symbol") == 0) j_set(e, "before_symbol", j_str(h->w[3]));
        else if (strcmp(mode, "after-symbol") == 0) j_set(e, "after_symbol", j_str(h->w[3]));
        else {
            snprintf(err, cap, "line %zu: insert mode must be before-line N, after-line N, before-symbol NAME or after-symbol NAME", at);
            json_free(e);
            return -1;
        }
        for (int k = 4; k < h->n; k++) {
            if (strncmp(h->w[k], "kind=", 5) == 0) j_set(e, "kind", j_str(h->w[k] + 5));
            else if (strncmp(h->w[k], "scope=", 6) == 0) j_set(e, "scope", j_str(h->w[k] + 6));
        }
        j_set(e, "new", j_strn(body->p ? body->p : "", body->len));
    } else if (strcmp(op, "symbol") == 0 && h->n >= 3) {
        j_set(e, "path", j_str(h->w[1]));
        j_set(e, "symbol", j_str(h->w[2]));
        for (int k = 3; k < h->n; k++) {
            if (strncmp(h->w[k], "kind=", 5) == 0) j_set(e, "kind", j_str(h->w[k] + 5));
            else if (strncmp(h->w[k], "scope=", 6) == 0) j_set(e, "scope", j_str(h->w[k] + 6));
        }
        j_set(e, "new", j_strn(body->p ? body->p : "", body->len));
    } else if (strcmp(op, "create") == 0 && h->n >= 2) {
        j_set(e, "path", j_str(h->w[1]));
        j_set(e, "create", j_bool(true));
        j_set(e, "new", j_strn(body->p ? body->p : "", body->len));
    } else {
        snprintf(err, cap, "line %zu: unknown or incomplete operation `%s`", at, op);
        json_free(e);
        return -1;
    }
    j_push(edits, e);
    return 0;
}

/* Parses an apply script into a patch request {edits:[...]}; NULL with `err` set on a syntax error. */
static J *parse_apply(const char *text, size_t len, char *err, size_t cap) {
    J *edits = j_arr();
    Words8 head = {0};
    Buf body, neu;
    buf_init(&body);
    buf_init(&neu);
    bool open = false, in_new = false;
    size_t lineno = 0, op_line = 0;
    int rc = 0;
    for (size_t i = 0; i < len && rc == 0;) {
        size_t j = i;
        while (j < len && text[j] != '\n') j++;
        size_t ll = j - i;
        if (ll > 0 && text[i + ll - 1] == '\r') ll--;
        lineno++;
        bool is_head = ll >= 3 && memcmp(text + i, "@@@", 3) == 0 && (ll == 3 || text[i + 3] == ' ');
        bool is_with = is_head && open && !in_new && ll >= 8 && memcmp(text + i, "@@@ with", 8) == 0 && (ll == 8 || text[i + 8] == ' ') &&
                       strcmp(head.w[0], "replace") == 0;
        if (is_with) {
            in_new = true;
        } else if (is_head) {
            if (open) rc = apply_finish(edits, &head, &body, &neu, in_new, op_line, err, cap);
            words8_free(&head);
            body.len = neu.len = 0;
            in_new = false;
            open = false;
            if (rc == 0) {
                split_words(text + i + 3, ll - 3, &head);
                if (head.n == 0) {
                    snprintf(err, cap, "line %zu: `@@@` needs an operation", lineno);
                    rc = -1;
                }
                open = true;
                op_line = lineno;
            }
        } else if (open) {
            Buf *dst = in_new ? &neu : &body;
            buf_put(dst, text + i, ll);
            buf_putc(dst, '\n');
        } else if (ll > 0) {
            snprintf(err, cap, "line %zu: text before the first `@@@` header", lineno);
            rc = -1;
        }
        i = j + 1;
    }
    if (rc == 0 && open) rc = apply_finish(edits, &head, &body, &neu, in_new, op_line, err, cap);
    words8_free(&head);
    buf_free(&body);
    buf_free(&neu);
    if (rc == 0 && edits->n == 0) {
        snprintf(err, cap, "the script has no operations");
        rc = -1;
    }
    if (rc != 0) {
        json_free(edits);
        return NULL;
    }
    J *req = j_obj();
    j_set(req, "edits", edits);
    return req;
}

/* Some agent shells halve doubled backslashes before a program sees them, so `\\n` (a backslash and an n
 * inside a C string, say) cannot be typed. `@BS@` is a backslash that survives: it is replaced in every
 * command-line argument and in `apply` scripts. Text read from files (--new-file, --old-file) is verbatim. */
static size_t unbs_n(char *s, size_t n) {
    size_t w = 0;
    for (size_t i = 0; i < n;) {
        if (i + 4 <= n && memcmp(s + i, "@BS@", 4) == 0) {
            s[w++] = '\\';
            i += 4;
        } else {
            s[w++] = s[i++];
        }
    }
    if (w < n) s[w] = 0;
    return w;
}

/* flush with a transparent three-way merge of files that changed on disk since the baseline */
static int flush_with_merge(Store *st, const char *const *names, size_t n) {
    if (store_flush(st, names, n, NULL) == 0) return 0;
    if (!strstr(na_errmsg(), "disk_conflict")) return -1;
    int conflicts = 0;
    for (size_t i = 0; i < n; i++) {
        J *rep;
        if (store_merge_external(st, names[i], "auto", false, &rep) != 0) return -1;
        const char *status = j_get(rep, "status")->s;
        if (strcmp(status, "conflict") == 0) {
            const J *c = j_get(rep, "conflicts");
            fprintf(stderr, "nextagent: %s changed outside nextagent and conflicts with this edit", names[i]);
            if (c && c->n) fprintf(stderr, " (lines %lld-%lld)", (long long)j_get(c->items[0], "first_line")->i, (long long)j_get(c->items[0], "last_line")->i);
            fprintf(stderr, "; resolve: nextagent merge %s --theirs | --ours\n", names[i]);
            conflicts++;
        } else if (strcmp(status, "merged") == 0 || strcmp(status, "fast_forward") == 0) {
            fprintf(stderr, "nextagent: merged outside changes in %s\n", names[i]);
        }
        json_free(rep);
    }
    if (conflicts) {
        na_seterr("disk_conflict: %d file(s) not written", conflicts);
        return -1;
    }
    return store_flush(st, names, n, NULL);
}

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
    for (int a = 1; a < argc; a++) unbs_n(argv[a], strlen(argv[a])); /* @BS@ -> backslash, see unbs_n */
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
    for (int k = 0; k < nargs;) { /* global flags are accepted after the command too */
        if (strcmp(args[k], "--brief") == 0 || strcmp(args[k], "--no-sync") == 0) {
            if (args[k][2] == 'b') g_brief = true;
            else g_autosync = false;
            memmove(args + k, args + k + 1, (size_t)(nargs - k - 1) * sizeof *args);
            nargs--;
        } else {
            k++;
        }
    }

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
    /* One-shot commands that address symbols or refactor use the language servers of non-C languages (started on
     * first use). `locate` and `context` stay on the cheap path (they would outline every file); find, callers and
     * callees ask the servers for the symbols of their languages. NEXTAGENT_NO_LSP=1 turns this off. */
    LspHub *cli_hub = NULL;
    {
        static const char *const lsp_cmds[] = {"outline", "select",     "view",   "replace-symbol", "insert", "apply",
                                               "patch",   "replace",    "rename", "references",     "add-param", "find", "callers", "callees", NULL};
        bool want = !getenv("NEXTAGENT_NO_LSP");
        bool listed = false;
        for (int k = 0; lsp_cmds[k]; k++)
            if (strcmp(cmd, lsp_cmds[k]) == 0) listed = true;
        if (want && listed) {
            cli_hub = lsp_hub_new(store_root(store), NULL, 20000);
            lsp_hub_scanner_for_c(cli_hub);
            rpc.hub = cli_hub;
        }
    }

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
    } else if (strcmp(cmd, "apply") == 0) {
        /* apply [FILE|-] [--label S] [--if-rev N]: the @@@ script format described above */
        const char *file = NULL, *label = NULL;
        int64_t if_rev = -1;
        for (int k = 0; k < nargs; k++) {
            if (k + 1 < nargs && strcmp(args[k], "--label") == 0) label = args[++k];
            else if (k + 1 < nargs && strcmp(args[k], "--if-rev") == 0 && parse_int(args[k + 1], &if_rev) == 0) k++;
            else if (!file && (args[k][0] != '-' || args[k][1] == 0)) file = args[k];
            else goto bad;
        }
        Buf data;
        buf_init(&data);
        if (file && strcmp(file, "-") != 0) {
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
        char perr[200];
        if (data.p) data.len = unbs_n(data.p, data.len);
        J *req = parse_apply(data.p ? data.p : "", data.len, perr, sizeof perr);
        buf_free(&data);
        if (!req) {
            fprintf(stderr, "nextagent: apply: %s\n", perr);
            code = 1;
            goto done;
        }
        if (label) j_set(req, "label", j_str(label));
        if (if_rev >= 0) j_set(req, "if_rev", j_int(if_rev));
        code = run_method(&rpc, "patch", req);
    } else if (strcmp(cmd, "create") == 0 || strcmp(cmd, "insert") == 0) {
        /* create PATH (--new S|--new-file F) ; insert PATH (--before-line N|--after-line N|--before-symbol NAME|--after-symbol NAME) (--new S|--new-file F) */
        bool is_create = cmd[0] == 'c';
        if (nargs < 1) goto bad;
        J *e = j_obj();
        j_set(e, "path", j_str(args[0]));
        if (is_create) j_set(e, "create", j_bool(true));
        char *new_buf = NULL;
        const char *neu = NULL;
        bool ok = true, located = is_create;
        for (int k = 1; k < nargs && ok; k++) {
            int64_t n;
            const char *a = args[k];
            if (k + 1 < nargs && strcmp(a, "--new") == 0) neu = args[++k];
            else if (k + 1 < nargs && strcmp(a, "--new-file") == 0) ok = read_text(args[++k], &new_buf) == 0, neu = new_buf;
            else if (!is_create && k + 1 < nargs && strcmp(a, "--before-line") == 0 && parse_int(args[k + 1], &n) == 0) j_set(e, "before_line", j_int(n)), k++, located = true;
            else if (!is_create && k + 1 < nargs && strcmp(a, "--after-line") == 0 && parse_int(args[k + 1], &n) == 0) j_set(e, "after_line", j_int(n)), k++, located = true;
            else if (!is_create && k + 1 < nargs && strcmp(a, "--before-symbol") == 0) j_set(e, "before_symbol", j_str(args[++k])), located = true;
            else if (!is_create && k + 1 < nargs && strcmp(a, "--after-symbol") == 0) j_set(e, "after_symbol", j_str(args[++k])), located = true;
            else if (!is_create && k + 1 < nargs && strcmp(a, "--kind") == 0) j_set(e, "kind", j_str(args[++k]));
            else if (!is_create && k + 1 < nargs && strcmp(a, "--scope") == 0) j_set(e, "scope", j_str(args[++k]));
            else ok = false;
        }
        if (!ok || !neu || !located) {
            free(new_buf);
            json_free(e);
            goto bad;
        }
        j_set(e, "new", j_str(neu));
        free(new_buf);
        J *req = j_obj(), *edits = j_arr();
        j_push(edits, e);
        j_set(req, "edits", edits);
        j_set(req, "label", j_str(cmd));
        code = run_method(&rpc, "patch", req);
    } else if (strcmp(cmd, "sub") == 0 || strcmp(cmd, "line") == 0 || strcmp(cmd, "del") == 0 || strcmp(cmd, "ins") == 0 ||
               strcmp(cmd, "append") == 0) {
        /* one-line edits for small changes (all go through `patch`, so they are one revision):
         *   sub OLD NEW PATH... [--all]   literal replacement in one or more files (every file must contain OLD)
         *   line PATH A[-B] TEXT          replace lines A..B (or just A) with TEXT
         *   del PATH A[-B]                delete lines
         *   ins PATH N TEXT               insert TEXT before line N
         *   append PATH TEXT              add TEXT after the last line */
        J *edits = j_arr();
        int64_t a = 0, b = 0, n = 0;
        bool good = true;
        if (cmd[0] == 's') {
            bool all = false;
            int paths = 0;
            for (int k = 2; k < nargs; k++) {
                if (strcmp(args[k], "--all") == 0) {
                    all = true;
                    continue;
                }
                J *e = j_obj();
                j_set(e, "path", j_str(args[k]));
                j_set(e, "old", j_str(args[0]));
                j_set(e, "new", j_str(args[1]));
                j_push(edits, e);
                paths++;
            }
            if (nargs < 3 || paths == 0) good = false;
            for (size_t k = 0; good && all && k < edits->n; k++) j_set(edits->items[k], "all", j_bool(true));
        } else if (cmd[0] == 'l' && nargs == 3 && parse_range(args[1], &a, &b) == 0) {
            J *e = j_obj();
            j_set(e, "path", j_str(args[0]));
            j_set(e, "start_line", j_int(a));
            j_set(e, "end_line", j_int(b));
            j_set(e, "new", j_str(args[2]));
            j_push(edits, e);
        } else if (cmd[0] == 'd' && nargs == 2 && parse_range(args[1], &a, &b) == 0) {
            J *e = j_obj();
            j_set(e, "path", j_str(args[0]));
            j_set(e, "start_line", j_int(a));
            j_set(e, "end_line", j_int(b));
            j_set(e, "new", j_str(""));
            j_push(edits, e);
        } else if (cmd[0] == 'i' && nargs == 3 && parse_int(args[1], &n) == 0) {
            J *e = j_obj();
            j_set(e, "path", j_str(args[0]));
            j_set(e, "before_line", j_int(n));
            j_set(e, "new", j_str(args[2]));
            j_push(edits, e);
        } else if (cmd[0] == 'a' && nargs == 2) {
            J *e = j_obj();
            j_set(e, "path", j_str(args[0]));
            j_set(e, "at_end", j_bool(true));
            j_set(e, "new", j_str(args[1]));
            j_push(edits, e);
        } else {
            good = false;
        }
        if (!good || edits->n == 0) {
            json_free(edits);
            goto bad;
        }
        J *req = j_obj();
        j_set(req, "edits", edits);
        j_set(req, "label", j_str(cmd));
        code = run_method(&rpc, "patch", req);
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
        rpc.autoflush = g_autosync;
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
    lsp_hub_free(cli_hub);
    store_close(store);
    return code;
bad:
    usage_for(cmd);
    rpc_release(&rpc);
    lsp_hub_free(cli_hub);
    store_close(store);
    return 2;
}
