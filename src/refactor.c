#include "refactor.h"
#include "c23.h"
#include "platform.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int list_workspace_sources(const char *root, size_t max_files, StrList *abs_files); /* lsp_build.c */

#define MAX_WORKSPACE_FILES 20000

/* ------------------------------------------------------------ tokenizer */
typedef struct {
    char k;  /* 'i' identifier, 'n' number, 's' string/char/include target, 'p' punctuation */
    char ch; /* punctuation character */
    size_t s, e; /* byte range */
} RTok;

typedef struct {
    RTok *v;
    size_t n, cap;
} Toks;

static void toks_push(Toks *t, char k, char ch, size_t s, size_t e) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->v = na_realloc(t->v, t->cap * sizeof *t->v);
    }
    t->v[t->n].k = k;
    t->v[t->n].ch = ch;
    t->v[t->n].s = s;
    t->v[t->n].e = e;
    t->n++;
}

static bool idc(unsigned char c) { return isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

static void tokenize(const char *s, size_t len, Toks *out) {
    memset(out, 0, sizeof *out);
    size_t i = 0;
    bool bol = true, directive = false;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            if (directive) toks_push(out, 'p', '\n', i, i); /* end-of-directive sentinel */
            directive = false;
            bol = true;
            i++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            i++;
        } else if (c == '\\' && i + 1 < len && (s[i + 1] == '\n' || (s[i + 1] == '\r' && i + 2 < len && s[i + 2] == '\n'))) {
            i += s[i + 1] == '\r' ? 3 : 2; /* line continuation */
        } else if (c == '/' && i + 1 < len && s[i + 1] == '/') {
            while (i < len && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < len && s[i + 1] == '\n') i++;
                i++;
            }
        } else if (c == '/' && i + 1 < len && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = i + 2 <= len ? i + 2 : len;
        } else if (c == '"' || c == '\'') {
            size_t st = i++;
            while (i < len && s[i] != (char)c) {
                if (s[i] == '\\' && i + 1 < len) i++;
                if (s[i] == '\n') break;
                i++;
            }
            if (i < len && s[i] == (char)c) i++;
            toks_push(out, 's', 0, st, i);
            bol = false;
        } else if (c == '#' && bol) {
            toks_push(out, 'p', '#', i, i + 1);
            i++;
            bol = false;
            directive = true;
            size_t j = i;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            size_t w = j;
            while (w < len && idc((unsigned char)s[w])) w++;
            size_t wl = w - j;
            if ((wl == 7 && strncmp(s + j, "include", 7) == 0) || (wl == 12 && strncmp(s + j, "include_next", 12) == 0) ||
                (wl == 6 && strncmp(s + j, "import", 6) == 0)) {
                toks_push(out, 'i', 0, j, w);
                size_t e = w;
                while (e < len && s[e] != '\n') e++;
                toks_push(out, 's', 0, w, e); /* header name: never an identifier */
                i = e;
            }
        } else if (isalpha(c) || c == '_' || c == '$' || c >= 0x80) {
            size_t st = i;
            while (i < len && idc((unsigned char)s[i])) i++;
            toks_push(out, 'i', 0, st, i);
            bol = false;
        } else if (isdigit(c)) {
            size_t st = i;
            while (i < len) {
                unsigned char d = (unsigned char)s[i];
                if (idc(d) || d == '.') {
                    i++;
                } else if ((d == '+' || d == '-') && i > st && strchr("eEpP", s[i - 1])) {
                    i++;
                } else if (d == '\'' && i + 1 < len && isalnum((unsigned char)s[i + 1])) {
                    i++;
                } else {
                    break;
                }
            }
            toks_push(out, 'n', 0, st, i);
            bol = false;
        } else {
            toks_push(out, 'p', (char)c, i, i + 1);
            i++;
            bol = false;
        }
    }
}

static bool tok_p(const Toks *t, size_t i, char ch) { return i < t->n && t->v[i].k == 'p' && t->v[i].ch == ch; }

/* Identifier reached through `.` or `->` (not the `...` ellipsis). */
static bool is_member(const Toks *t, size_t i) {
    if (i == 0) return false;
    if (tok_p(t, i - 1, '.')) return !(i >= 2 && tok_p(t, i - 2, '.') && t->v[i - 2].e == t->v[i - 1].s);
    return tok_p(t, i - 1, '>') && i >= 2 && tok_p(t, i - 2, '-') && t->v[i - 2].e == t->v[i - 1].s;
}

static const char *const KEYWORDS[] = {
    "alignas", "alignof", "auto", "bool", "break", "case", "char", "const", "constexpr", "continue", "default",
    "do", "double", "else", "enum", "extern", "false", "float", "for", "goto", "if", "inline", "int", "long",
    "nullptr", "register", "restrict", "return", "short", "signed", "sizeof", "static", "static_assert", "struct",
    "switch", "true", "typedef", "typeof", "typeof_unqual", "union", "unsigned", "void", "volatile", "while",
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert",
    "_Thread_local", "_BitInt", "_Decimal32", "_Decimal64", "_Decimal128"};

static bool is_keyword(const char *s) {
    for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++)
        if (strcmp(s, KEYWORDS[i]) == 0) return true;
    return false;
}

static bool valid_ident(const char *s) {
    if (!s || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (const char *p = s; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return false;
    return true;
}

/* -------------------------------------------------- byte -> codepoint */
typedef struct {
    const char *t;
    size_t b, cp;
} Cursor;

static size_t cur_cp(Cursor *c, size_t byte) {
    while (c->b < byte) {
        if (((unsigned char)c->t[c->b] & 0xC0) != 0x80) c->cp++;
        c->b++;
    }
    return c->cp;
}

typedef struct {
    size_t bs, be;
    char *text;
    size_t idx;
} BEdit;

typedef struct {
    BEdit *v;
    size_t n, cap;
} BEdits;

static void be_add(BEdits *l, size_t bs, size_t be, const char *text, size_t tlen) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = na_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n].bs = bs;
    l->v[l->n].be = be;
    l->v[l->n].text = na_strndup(text, tlen);
    l->v[l->n].idx = l->n;
    l->n++;
}

static void be_free(BEdits *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i].text);
    free(l->v);
    memset(l, 0, sizeof *l);
}

static int be_cmp(const void *a, const void *b) {
    const BEdit *x = a, *y = b;
    if (x->bs != y->bs) return x->bs < y->bs ? -1 : 1;
    if (x->be != y->be) return x->be < y->be ? -1 : 1;
    return x->idx < y->idx ? -1 : (x->idx > y->idx);
}

static int be_to_cs(BEdits *l, const char *text, size_t len, ChangeSet *cs) {
    (void)len;
    cs_init(cs);
    qsort(l->v, l->n, sizeof *l->v, be_cmp);
    Cursor c = {text, 0, 0};
    for (size_t i = 0; i < l->n; i++) {
        size_t s = cur_cp(&c, l->v[i].bs);
        size_t e = cur_cp(&c, l->v[i].be);
        cs_add(cs, s, e, l->v[i].text, strlen(l->v[i].text));
    }
    if (cs_validate(cs) != 0) {
        cs_free(cs);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------ workspace files */
static bool is_cfile(const char *p) {
    static const char *const ext[] = {".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"};
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++)
        if (path_has_suffix_ci(p, ext[i])) return true;
    return false;
}

static int ws_files(Store *st, const StrList *only, StrList *out) {
    sl_init(out);
    if (only && only->n) {
        for (size_t i = 0; i < only->n; i++) {
            char *c = store_canon(st, only->v[i]);
            if (!c) {
                sl_free(out);
                return -1;
            }
            if (!sl_has(out, c)) sl_push(out, c);
            free(c);
        }
        return 0;
    }
    const char *root = store_root(st);
    size_t rl = strlen(root);
    StrList abs;
    sl_init(&abs);
    if (list_workspace_sources(root, MAX_WORKSPACE_FILES, &abs) != 0) {
        sl_free(&abs);
        sl_free(out);
        return -1;
    }
    StrSet seen;
    ss_init(&seen);
    for (size_t i = 0; i < abs.n; i++) {
        const char *p = abs.v[i];
        if (strlen(p) > rl && memcmp(p, root, rl) == 0) {
            p += rl;
            while (*p == '/') p++;
        }
        if (ss_add(&seen, p)) sl_push(out, p);
    }
    sl_free(&abs);
    StrList virt;
    sl_init(&virt);
    if (store_snapshot_paths(st, &virt) == 0)
        for (size_t i = 0; i < virt.n; i++)
            if (is_cfile(virt.v[i]) && ss_add(&seen, virt.v[i])) sl_push(out, virt.v[i]);
    sl_free(&virt);
    ss_free(&seen);
    sl_sort(out);
    return 0;
}

/* Read a document; *text NULL when it is empty/non-UTF-8 (skipped). */
static int ws_read(Store *st, const char *name, char **text, size_t *len) {
    *text = NULL;
    *len = 0;
    char *t;
    size_t n;
    if (store_snapshot_text(st, name, &t, &n) != 0) return -1;
    if (!utf8_valid(t, n)) {
        free(t);
        return 0;
    }
    *text = t;
    *len = n;
    return 0;
}

static bool ident_eq(const char *s, const RTok *t, const char *name) {
    size_t n = strlen(name);
    return t->e - t->s == n && memcmp(s + t->s, name, n) == 0;
}

static int check_rev(Store *st, int64_t *if_rev) {
    int64_t head = store_head(st);
    if (*if_rev == STORE_NO_REV) {
        *if_rev = head;
        return 0;
    }
    if (*if_rev != head) return na_fail("stale_rev: expected %lld, current %lld", (long long)*if_rev, (long long)head);
    return 0;
}

/* --------------------------------------------------------- references */
int rf_references(Store *st, const char *name, const StrList *only, bool members, J **out) {
    if (!valid_ident(name)) return na_fail("invalid parameter: name must be a C identifier");
    StrList files;
    if (ws_files(st, only, &files) != 0) return -1;
    J *arr = j_arr();
    for (size_t f = 0; f < files.n; f++) {
        char *text;
        size_t len;
        if (ws_read(st, files.v[f], &text, &len) != 0) {
            sl_free(&files);
            json_free(arr);
            return -1;
        }
        if (!text) continue;
        Toks t;
        tokenize(text, len, &t);
        Snapshot *snap = snap_new(0, text, len);
        Cursor cur = {text, 0, 0};
        for (size_t i = 0; i < t.n; i++) {
            if (t.v[i].k != 'i' || !ident_eq(text, &t.v[i], name)) continue;
            bool member = is_member(&t, i);
            if (member && !members) continue;
            size_t s = cur_cp(&cur, t.v[i].s), e = cur_cp(&cur, t.v[i].e), line = 0, col = 0;
            snap_position(snap, s, &line, &col);
            J *o = j_obj();
            j_set(o, "path", j_str(files.v[f]));
            j_set(o, "start", j_int((int64_t)s));
            j_set(o, "end", j_int((int64_t)e));
            j_set(o, "line", j_int((int64_t)line));
            j_set(o, "column", j_int((int64_t)col));
            j_set(o, "member", j_bool(member));
            j_set(o, "call", j_bool(tok_p(&t, i + 1, '(')));
            j_push(arr, o);
        }
        snap_free(snap);
        free(t.v);
        free(text);
    }
    sl_free(&files);
    J *o = j_obj();
    j_set(o, "count", j_int((int64_t)arr->n));
    j_set(o, "references", arr);
    *out = o;
    return 0;
}

/* -------------------------------------------------------------- rename */
typedef struct {
    char **names;
    ChangeSet *sets;
    size_t n, cap;
} Plan;

static void plan_add(Plan *p, const char *name, ChangeSet cs) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->names = na_realloc(p->names, p->cap * sizeof *p->names);
        p->sets = na_realloc(p->sets, p->cap * sizeof *p->sets);
    }
    p->names[p->n] = na_strdup(name);
    p->sets[p->n] = cs;
    p->n++;
}

static void plan_free(Plan *p) {
    for (size_t i = 0; i < p->n; i++) {
        free(p->names[i]);
        cs_free(&p->sets[i]);
    }
    free(p->names);
    free(p->sets);
    memset(p, 0, sizeof *p);
}

static int plan_commit(Store *st, Plan *p, int64_t if_rev, const char *label, const char *session,
                       const Validator *validator, EditResult *res) {
    return store_edit(st, (const char *const *)p->names, p->sets, p->n, if_rev, label, session ? session : "jsonrpc",
                      validator, res);
}

int rf_rename(Store *st, const char *name, const char *new_name, const StrList *only, bool members,
              bool allow_existing, int64_t if_rev, const char *label, const char *session,
              const Validator *validator, EditResult *res, RfCounts *counts) {
    if (!valid_ident(name) || is_keyword(name)) return na_fail("invalid parameter: name must be a C identifier");
    if (!valid_ident(new_name) || is_keyword(new_name))
        return na_fail("invalid parameter: new_name must be a valid, non-keyword C identifier");
    if (strcmp(name, new_name) == 0) return na_fail("invalid parameter: new_name equals name");
    if (check_rev(st, &if_rev) != 0) return -1;
    StrList files;
    if (ws_files(st, only, &files) != 0) return -1;
    Plan plan;
    memset(&plan, 0, sizeof plan);
    size_t total = 0;
    int rc = 0;
    for (size_t f = 0; f < files.n && rc == 0; f++) {
        char *text;
        size_t len;
        if ((rc = ws_read(st, files.v[f], &text, &len)) != 0) break;
        if (!text) continue;
        Toks t;
        tokenize(text, len, &t);
        BEdits edits;
        memset(&edits, 0, sizeof edits);
        for (size_t i = 0; i < t.n; i++) {
            if (t.v[i].k != 'i') continue;
            if (!allow_existing && ident_eq(text, &t.v[i], new_name) && !is_member(&t, i)) {
                rc = na_fail("name_conflict: %s is already used in %s", new_name, files.v[f]);
                break;
            }
            if (!ident_eq(text, &t.v[i], name)) continue;
            if (!members && is_member(&t, i)) continue;
            be_add(&edits, t.v[i].s, t.v[i].e, new_name, strlen(new_name));
            total++;
        }
        if (rc == 0 && edits.n) {
            ChangeSet cs;
            if ((rc = be_to_cs(&edits, text, len, &cs)) == 0) plan_add(&plan, files.v[f], cs);
        }
        be_free(&edits);
        free(t.v);
        free(text);
    }
    sl_free(&files);
    if (rc == 0 && total == 0) rc = na_fail("symbol_not_found: %s", name);
    if (rc == 0) rc = plan_commit(st, &plan, if_rev, label ? label : "rename", session, validator, res);
    if (rc == 0 && counts) {
        counts->sites = total;
        counts->calls = 0;
    }
    plan_free(&plan);
    return rc;
}

/* ----------------------------------------------------------- add_param */
typedef struct {
    size_t s, e; /* trimmed byte range of one argument */
} Arg;

/* Match the parenthesis opening at token `open`; returns the closing token index or (size_t)-1. */
static size_t match_paren(const Toks *t, size_t open) {
    int depth = 0;
    for (size_t i = open; i < t->n; i++) {
        if (tok_p(t, i, '(')) depth++;
        else if (tok_p(t, i, ')') && --depth == 0) return i;
        else if (tok_p(t, i, ';')) return (size_t)-1; /* a call or prototype never contains ';' */
    }
    return (size_t)-1;
}

/* Split the tokens strictly between `open` and `close` at depth-0 commas. */
static size_t split_args(const Toks *t, size_t open, size_t close, Arg *args, size_t max) {
    size_t n = 0;
    int depth = 0;
    size_t first = open + 1;
    if (first == close) return 0;
    for (size_t i = open + 1; i <= close; i++) {
        bool end = i == close;
        if (!end) {
            if (tok_p(t, i, '(') || tok_p(t, i, '[') || tok_p(t, i, '{')) depth++;
            else if (tok_p(t, i, ')') || tok_p(t, i, ']') || tok_p(t, i, '}')) depth--;
        }
        if (end || (depth == 0 && tok_p(t, i, ','))) {
            if (n < max) {
                args[n].s = first < i ? t->v[first].s : t->v[i].s;
                args[n].e = first < i ? t->v[i - 1].e : t->v[i].s;
            }
            n++;
            first = i + 1;
        }
    }
    return n;
}

static bool expr_keyword(const char *s, const RTok *t) {
    static const char *const kw[] = {"return", "sizeof", "case", "else", "do", "goto", "typeof", "alignof", "_Alignof",
                                     "defined", "typeof_unqual", "if", "while", "for", "switch", "new", "delete", "throw"};
    for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++)
        if (strlen(kw[i]) == t->e - t->s && memcmp(s + t->s, kw[i], t->e - t->s) == 0) return true;
    return false;
}

/* Declaration/definition heuristics: `<type> [*...] name ( ... ) {|;|,` */
static bool looks_like_decl(const Toks *t, const char *s, size_t id, size_t rparen) {
    if (!(tok_p(t, rparen + 1, '{') || tok_p(t, rparen + 1, ';') || tok_p(t, rparen + 1, ','))) return false;
    size_t j = id;
    if (j == 0) return false;
    j--;
    while (tok_p(t, j, '*')) {
        if (j == 0) return false;
        j--;
    }
    if (t->v[j].k != 'i' || expr_keyword(s, &t->v[j])) return false;
    if (j == 0) return true;
    const RTok *b = &t->v[j - 1];
    if (b->k == 'i') return !expr_keyword(s, b);
    return b->k == 'p' && (b->ch == ';' || b->ch == '\n' || b->ch == '}' || b->ch == '{' || b->ch == ')' || b->ch == '*' || b->ch == ']');
}

typedef struct {
    size_t id, open, close;
    bool decl;
    bool is_def;
    Arg *args;
    size_t nargs;
} Site;

typedef struct {
    char *path, *text;
    size_t len;
    Toks t;
    Site *sites;
    size_t nsites;
} FileSites;

int rf_add_param(Store *st, const char *function, const char *param, const char *default_arg,
                 int64_t position, const StrList *only, int64_t if_rev, const char *label,
                 const char *session, const Validator *validator, EditResult *res, RfCounts *counts) {
    if (!valid_ident(function) || is_keyword(function)) return na_fail("invalid parameter: function must be a C identifier");
    if (!param || !param[0] || strpbrk(param, "\r\n")) return na_fail("invalid parameter: param must be one line of text");
    if (position < -1) return na_fail("invalid parameter: position must be >= -1");
    if (check_rev(st, &if_rev) != 0) return -1;
    StrList names;
    if (ws_files(st, only, &names) != 0) return -1;

    FileSites *fs = NULL;
    size_t nfs = 0, ndecl = 0, ncall = 0;
    int rc = 0;
    for (size_t f = 0; f < names.n && rc == 0; f++) {
        char *text;
        size_t len;
        if ((rc = ws_read(st, names.v[f], &text, &len)) != 0) break;
        if (!text) continue;
        FileSites cur;
        memset(&cur, 0, sizeof cur);
        cur.path = na_strdup(names.v[f]);
        cur.text = text;
        cur.len = len;
        tokenize(text, len, &cur.t);
        size_t cap = 0;
        for (size_t i = 0; i < cur.t.n; i++) {
            if (cur.t.v[i].k != 'i' || !ident_eq(text, &cur.t.v[i], function)) continue;
            if (is_member(&cur.t, i) || !tok_p(&cur.t, i + 1, '(')) continue;
            size_t close = match_paren(&cur.t, i + 1);
            if (close == (size_t)-1) continue;
            if (cur.nsites == cap) {
                cap = cap ? cap * 2 : 8;
                cur.sites = na_realloc(cur.sites, cap * sizeof *cur.sites);
            }
            Site *s = &cur.sites[cur.nsites++];
            memset(s, 0, sizeof *s);
            s->id = i;
            s->open = i + 1;
            s->close = close;
            s->decl = looks_like_decl(&cur.t, text, i, close);
            s->is_def = s->decl && tok_p(&cur.t, close + 1, '{');
            size_t need = close - s->open; /* upper bound on args */
            s->args = na_alloc((need + 1) * sizeof *s->args);
            s->nargs = split_args(&cur.t, s->open, close, s->args, need + 1);
            if (s->decl) ndecl++;
            else ncall++;
        }
        if (cur.nsites) {
            fs = na_realloc(fs, (nfs + 1) * sizeof *fs);
            fs[nfs++] = cur;
        } else {
            free(cur.path);
            free(cur.text);
            free(cur.t.v);
        }
    }
    sl_free(&names);
    if (rc == 0 && ndecl == 0) rc = na_fail("symbol_not_found: no declaration of function %s", function);
    if (rc == 0 && ncall && (!default_arg || !default_arg[0]))
        rc = na_fail("missing parameter: default (needed for %zu call site(s) of %s)", ncall, function);

    /* Insertion index: explicit, else the definition's parameter count (falling back to a prototype). */
    int64_t resolved = -1;
    if (rc == 0 && position >= 0) resolved = position;
    else if (rc == 0) {
        int64_t proto = -1, def = -1;
        for (size_t f = 0; f < nfs; f++)
            for (size_t k = 0; k < fs[f].nsites; k++) {
                const Site *s = &fs[f].sites[k];
                if (!s->decl) continue;
                size_t n = s->nargs;
                if (n == 1 && s->args[0].e - s->args[0].s == 4 && memcmp(fs[f].text + s->args[0].s, "void", 4) == 0) n = 0;
                if (n && s->args[n - 1].e - s->args[n - 1].s == 3 && memcmp(fs[f].text + s->args[n - 1].s, "...", 3) == 0) n--;
                if (s->is_def && def < 0) def = (int64_t)n;
                if (proto < 0) proto = (int64_t)n;
            }
        resolved = def >= 0 ? def : proto;
    }

    Plan plan;
    memset(&plan, 0, sizeof plan);
    for (size_t f = 0; f < nfs && rc == 0; f++) {
        FileSites *q = &fs[f];
        BEdits edits;
        memset(&edits, 0, sizeof edits);
        for (size_t k = 0; k < q->nsites && rc == 0; k++) {
            Site *s = &q->sites[k];
            const char *ins = s->decl ? param : default_arg;
            size_t n = s->nargs;
            bool is_void = n == 1 && s->args[0].e - s->args[0].s == 4 && memcmp(q->text + s->args[0].s, "void", 4) == 0;
            size_t lp = q->t.v[s->open].e, rp = q->t.v[s->close].s;
            if (s->decl && is_void) {
                if (resolved != 0) {
                    rc = na_fail("position out of range for %s: %s takes no parameters", q->path, function);
                    break;
                }
                be_add(&edits, s->args[0].s, s->args[0].e, ins, strlen(ins));
                continue;
            }
            if (n == 0) {
                if (resolved != 0) {
                    rc = na_fail("position out of range for %s (%s has no arguments here)", q->path, function);
                    break;
                }
                be_add(&edits, lp, rp, ins, strlen(ins));
                continue;
            }
            size_t fixed = n;
            if (s->decl && s->args[n - 1].e - s->args[n - 1].s == 3 && memcmp(q->text + s->args[n - 1].s, "...", 3) == 0)
                fixed = n - 1;
            if ((size_t)resolved > fixed) {
                rc = na_fail("position out of range for %s: %s has only %zu argument(s) at one site", q->path, function, fixed);
                break;
            }
            if ((size_t)resolved == n) {
                Buf b;
                buf_init(&b);
                buf_printf(&b, ", %s", ins);
                be_add(&edits, s->args[n - 1].e, s->args[n - 1].e, b.p, b.len);
                buf_free(&b);
            } else {
                Buf b;
                buf_init(&b);
                buf_printf(&b, "%s, ", ins);
                be_add(&edits, s->args[resolved].s, s->args[resolved].s, b.p, b.len);
                buf_free(&b);
            }
        }
        if (rc == 0 && edits.n) {
            ChangeSet cs;
            if ((rc = be_to_cs(&edits, q->text, q->len, &cs)) == 0) plan_add(&plan, q->path, cs);
        }
        be_free(&edits);
    }
    for (size_t f = 0; f < nfs; f++) {
        for (size_t k = 0; k < fs[f].nsites; k++) free(fs[f].sites[k].args);
        free(fs[f].sites);
        free(fs[f].path);
        free(fs[f].text);
        free(fs[f].t.v);
    }
    free(fs);
    if (rc == 0) rc = plan_commit(st, &plan, if_rev, label ? label : "add_param", session, validator, res);
    if (rc == 0 && counts) {
        counts->sites = ndecl;
        counts->calls = ncall;
    }
    plan_free(&plan);
    return rc;
}

/* --------------------------------------------------------- add_include */
int rf_add_include(Store *st, const char *target, const char *header, bool system, int64_t if_rev,
                   const char *label, const char *session, const Validator *validator,
                   EditResult *res, bool *changed) {
    *changed = false;
    if (!header || !header[0] || strpbrk(header, "\r\n\"<>"))
        return na_fail("invalid parameter: header must be a plain file name");
    if (check_rev(st, &if_rev) != 0) return -1;
    char *canon = store_canon(st, target);
    if (!canon) return -1;
    char *text;
    size_t len;
    if (store_snapshot_text(st, canon, &text, &len) != 0) {
        free(canon);
        return -1;
    }
    Buf line;
    buf_init(&line);
    buf_printf(&line, system ? "#include <%s>" : "#include \"%s\"", header);
    const char *eol = "\n";
    for (size_t i = 0; i < len; i++)
        if (text[i] == '\n') {
            if (i > 0 && text[i - 1] == '\r') eol = "\r\n";
            break;
        }
    size_t insert = 0, last_inc_end = (size_t)-1;
    bool dup = false;
    size_t i = 0, pos_after_lead = (size_t)-1;
    bool in_block = false, guard_pending = false;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n') e++;
        size_t next = e < len ? e + 1 : len;
        size_t a = i, b = e;
        while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
        while (b > a && (text[b - 1] == '\r' || text[b - 1] == ' ' || text[b - 1] == '\t')) b--;
        if (in_block) {
            for (size_t k = a; k + 1 < b; k++)
                if (text[k] == '*' && text[k + 1] == '/') in_block = false;
        } else if (a < b && text[a] == '/' && a + 1 < b && text[a + 1] == '*') {
            in_block = true;
            for (size_t k = a + 2; k + 1 < b; k++)
                if (text[k] == '*' && text[k + 1] == '/') in_block = false;
        } else if (a == b || (a + 1 < b && text[a] == '/' && text[a + 1] == '/')) {
            /* blank or line comment */
        } else {
            if (pos_after_lead == (size_t)-1) pos_after_lead = i;
            bool was_pending = guard_pending;
            guard_pending = false;
            if (text[a] == '#') {
                size_t d = a + 1;
                while (d < b && (text[d] == ' ' || text[d] == '	')) d++;
                if (b - d >= 7 && strncmp(text + d, "include", 7) == 0 && (b - d == 7 || !idc((unsigned char)text[d + 7]))) {
                    if (b - a == line.len && memcmp(text + a, line.p, line.len) == 0) dup = true;
                    last_inc_end = next;
                } else if (b - d >= 6 && strncmp(text + d, "ifndef", 6) == 0 && last_inc_end == (size_t)-1) {
                    guard_pending = true;
                } else if (was_pending && b - d >= 6 && strncmp(text + d, "define", 6) == 0) {
                    insert = next;
                    last_inc_end = (size_t)-2; /* marker: guard found, no include yet */
                }
            }
        }
        i = next;
    }
    int rc = 0;
    if (dup) {
        res->rev = store_head(st);
        sl_init(&res->paths);
    } else {
        size_t at;
        if (last_inc_end != (size_t)-1 && last_inc_end != (size_t)-2) at = last_inc_end;
        else if (last_inc_end == (size_t)-2) at = insert;
        else at = pos_after_lead == (size_t)-1 ? len : pos_after_lead;
        Buf ins;
        buf_init(&ins);
        if (at == len && len > 0 && text[len - 1] != '\n') buf_puts(&ins, eol);
        buf_put(&ins, line.p, line.len);
        buf_puts(&ins, eol);
        if (last_inc_end == (size_t)-1 && pos_after_lead != (size_t)-1 && at == pos_after_lead) buf_puts(&ins, eol);
        size_t cp = utf8_cp_count(text, at);
        ChangeSet cs;
        cs_init(&cs);
        cs_add(&cs, cp, cp, ins.p, ins.len);
        const char *names[1] = {canon};
        rc = store_edit(st, names, &cs, 1, if_rev, label ? label : "add_include", session ? session : "jsonrpc", validator, res);
        cs_free(&cs);
        buf_free(&ins);
        *changed = rc == 0;
    }
    buf_free(&line);
    free(text);
    free(canon);
    return rc;
}

/* -------------------------------------------------------- find_symbols */
static bool contains_ci(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return true;
    for (const char *p = hay; *p; p++) {
        size_t k = 0;
        while (k < n && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)needle[k])) k++;
        if (k == n) return true;
    }
    return false;
}

int rf_find_symbols(Store *st, const char *query, const char *kind, size_t limit, J **out) {
    if (limit == 0 || limit > 1000) limit = 100;
    StrList files;
    if (ws_files(st, NULL, &files) != 0) return -1;
    J *arr = j_arr();
    bool truncated = false;
    for (size_t f = 0; f < files.n && !truncated; f++) {
        char *text;
        size_t len;
        if (ws_read(st, files.v[f], &text, &len) != 0) {
            sl_free(&files);
            json_free(arr);
            return -1;
        }
        if (!text) continue;
        Snapshot *snap = snap_new(0, text, len);
        SymbolList list;
        if (c23_outline(snap, false, &list) == 0) {
            for (size_t i = 0; i < list.n; i++) {
                const Symbol *s = &list.v[i];
                if (kind && strcmp(kind, s->kind) != 0) continue;
                if (!contains_ci(s->name, query)) continue;
                if (arr->n >= limit) {
                    truncated = true;
                    break;
                }
                J *o = symbol_to_json(s);
                j_set(o, "path", j_str(files.v[f]));
                j_push(arr, o);
            }
            symbols_free(&list);
        }
        snap_free(snap);
        free(text);
    }
    sl_free(&files);
    J *o = j_obj();
    j_set(o, "count", j_int((int64_t)arr->n));
    j_set(o, "truncated", j_bool(truncated));
    j_set(o, "symbols", arr);
    *out = o;
    return 0;
}
