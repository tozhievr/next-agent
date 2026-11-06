#include "c23.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum { T_ID, T_NUM, T_STR, T_PUNCT } TokKind;

typedef struct {
    TokKind kind;
    char ch; /* T_PUNCT */
    size_t s, e; /* byte range */
} Tok;

typedef struct {
    const char *src;
    size_t len;
    Tok *t;
    size_t n, cap;
    size_t *cp_check; /* codepoint count at byte multiples of CHECK */
} Scan;

#define CHECK 256

static void tok_push(Scan *sc, TokKind k, char ch, size_t s, size_t e) {
    if (sc->n == sc->cap) {
        sc->cap = sc->cap ? sc->cap * 2 : 256;
        sc->t = na_realloc(sc->t, sc->cap * sizeof *sc->t);
    }
    sc->t[sc->n].kind = k;
    sc->t[sc->n].ch = ch;
    sc->t[sc->n].s = s;
    sc->t[sc->n].e = e;
    sc->n++;
}

static bool id_start(unsigned char c) { return isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
static bool id_char(unsigned char c) { return isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

static size_t skip_quoted(const char *s, size_t len, size_t i, char q) {
    i++;
    while (i < len && s[i] != q) {
        if (s[i] == '\\' && i + 1 < len) i++;
        if (s[i] == '\n' && q == '"') break; /* unterminated: stop at line end */
        i++;
    }
    return i < len ? i + 1 : len;
}

static void tokenize(Scan *sc) {
    const char *s = sc->src;
    size_t len = sc->len, i = 0;
    bool line_start = true;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            line_start = true;
            i++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            i++;
        } else if (c == '/' && i + 1 < len && s[i + 1] == '/') {
            while (i < len && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < len && s[i + 1] == '\n') i++;
                i++;
            }
        } else if (c == '/' && i + 1 < len && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i = i + 2 <= len ? i + 2 : len;
        } else if (c == '#' && line_start) {
            /* preprocessor directive: to end of (continued) line */
            while (i < len && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < len && (s[i + 1] == '\n' || (s[i + 1] == '\r'))) {
                    i += 2;
                    if (i < len && s[i - 1] == '\r' && s[i] == '\n') i++;
                    continue;
                }
                if (s[i] == '/' && i + 1 < len && s[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++;
                    i = i + 2 <= len ? i + 2 : len;
                    continue;
                }
                if (s[i] == '"' || s[i] == '\'') {
                    i = skip_quoted(s, len, i, s[i]);
                    continue;
                }
                i++;
            }
        } else if (c == '"' || c == '\'') {
            size_t e = skip_quoted(s, len, i, (char)c);
            tok_push(sc, T_STR, 0, i, e);
            i = e;
            line_start = false;
        } else if (id_start(c)) {
            size_t b = i;
            while (i < len && id_char((unsigned char)s[i])) i++;
            /* encoding prefixes glue onto the following literal */
            if (i < len && (s[i] == '"' || s[i] == '\'') &&
                ((i - b == 1 && (s[b] == 'L' || s[b] == 'u' || s[b] == 'U')) ||
                 (i - b == 2 && s[b] == 'u' && s[b + 1] == '8'))) {
                size_t e = skip_quoted(s, len, i, s[i]);
                tok_push(sc, T_STR, 0, b, e);
                i = e;
            } else {
                tok_push(sc, T_ID, 0, b, i);
            }
            line_start = false;
        } else if (isdigit(c) || (c == '.' && i + 1 < len && isdigit((unsigned char)s[i + 1]))) {
            size_t b = i;
            i++;
            while (i < len) {
                unsigned char d = (unsigned char)s[i];
                if (isalnum(d) || d == '.' || d == '_') {
                    i++;
                } else if ((d == '+' || d == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E' ||
                                                     s[i - 1] == 'p' || s[i - 1] == 'P')) {
                    i++;
                } else if (d == '\'' && i + 1 < len && isalnum((unsigned char)s[i + 1])) {
                    i++;
                } else {
                    break;
                }
            }
            tok_push(sc, T_NUM, 0, b, i);
            line_start = false;
        } else {
            tok_push(sc, T_PUNCT, (char)c, i, i + 1);
            i++;
            line_start = false;
        }
    }
}

static bool tok_is(const Scan *sc, size_t i, const char *word) {
    const Tok *t = &sc->t[i];
    size_t wl = strlen(word);
    return t->kind == T_ID && t->e - t->s == wl && memcmp(sc->src + t->s, word, wl) == 0;
}

static bool punct(const Scan *sc, size_t i, char ch) {
    return i < sc->n && sc->t[i].kind == T_PUNCT && sc->t[i].ch == ch;
}

static const char *const KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
    "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
    "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", "alignas", "alignof", "bool",
    "constexpr", "false", "nullptr", "static_assert", "thread_local", "true", "typeof",
    "typeof_unqual", "_BitInt", "__inline", "__inline__", "__restrict", "__restrict__", "__const",
    "__volatile__", "__extension__", NULL};

/* keywords/attributes that are followed by a parenthesized group to ignore */
static const char *const GROUP_WORDS[] = {"__attribute__", "__attribute", "__declspec", "_Alignas", "alignas",
                                          "__asm__", "__asm", "asm", "typeof", "typeof_unqual",
                                          "__typeof__", "_Alignof", "alignof", "sizeof", "_BitInt",
                                          "_Atomic", NULL};

static bool in_list(const Scan *sc, size_t i, const char *const *list) {
    for (size_t k = 0; list[k]; k++)
        if (tok_is(sc, i, list[k])) return true;
    return false;
}

static bool is_ident(const Scan *sc, size_t i) {
    return sc->t[i].kind == T_ID && !in_list(sc, i, KEYWORDS) && !in_list(sc, i, GROUP_WORDS);
}

/* Index of the token matching the opener at `i`, or n-1 when unbalanced. */
static size_t match(const Scan *sc, size_t i) {
    int depth = 0;
    for (size_t k = i; k < sc->n; k++) {
        if (sc->t[k].kind != T_PUNCT) continue;
        char c = sc->t[k].ch;
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (depth <= 0) return k;
        }
    }
    return sc->n ? sc->n - 1 : 0;
}

static bool opener(const Scan *sc, size_t i) {
    return sc->t[i].kind == T_PUNCT && (sc->t[i].ch == '(' || sc->t[i].ch == '[' || sc->t[i].ch == '{');
}

/* Name of the first declarator in tokens [a, b), or -1. */
static long declarator_name(const Scan *sc, size_t a, size_t b) {
    long last = -1;
    size_t k = a;
    while (k < b) {
        const Tok *t = &sc->t[k];
        if (t->kind == T_ID) {
            if (in_list(sc, k, GROUP_WORDS) && punct(sc, k + 1, '(')) {
                k = match(sc, k + 1) + 1;
                continue;
            }
            if (is_ident(sc, k)) {
                bool tag = k > a && (tok_is(sc, k - 1, "struct") || tok_is(sc, k - 1, "union") ||
                                     tok_is(sc, k - 1, "enum"));
                if (!tag) last = (long)k;
            }
            k++;
            continue;
        }
        if (t->kind == T_PUNCT) {
            char c = t->ch;
            if (c == '(') {
                size_t close = match(sc, k);
                if (last >= 0) return last; /* function declarator */
                for (size_t m = k + 1; m < close && m < b; m++)
                    if (sc->t[m].kind == T_ID && is_ident(sc, m)) return (long)m;
                k = close + 1;
                continue;
            }
            if (c == '{') {
                k = match(sc, k) + 1;
                continue;
            }
            if (c == '[') {
                if (punct(sc, k + 1, '[')) { /* [[attribute]] */
                    k = match(sc, k) + 1;
                    continue;
                }
                if (last >= 0) return last;
                k = match(sc, k) + 1;
                continue;
            }
            if (c == ',' || c == ';' || c == '=' || c == ':') {
                if (last >= 0) return last;
            }
        }
        k++;
    }
    return last;
}

/* ---- offsets ---- */
static size_t byte_to_cp(const Scan *sc, size_t b) {
    size_t blk = b / CHECK;
    size_t cp = sc->cp_check[blk];
    size_t from = blk * CHECK;
    for (size_t i = from; i < b; i++)
        if (((unsigned char)sc->src[i] & 0xC0) != 0x80) cp++;
    return cp;
}

static void add_symbol(SymbolList *out, const Scan *sc, const Snapshot *snap, const char *name,
                       size_t name_len, const char *kind, size_t bstart, size_t bend, const char *scope) {
    if (out->n == out->cap) {
        out->cap = out->cap ? out->cap * 2 : 16;
        out->v = na_realloc(out->v, out->cap * sizeof *out->v);
    }
    Symbol *s = &out->v[out->n++];
    s->name = na_strndup(name, name_len);
    s->kind = na_strdup(kind);
    s->scope = na_strdup(scope);
    s->start = byte_to_cp(sc, bstart);
    s->end = byte_to_cp(sc, bend);
    size_t line = 1, col = 0;
    snap_position(snap, s->start, &line, &col);
    s->line = line;
    s->column = col;
}

void symbols_free(SymbolList *l) {
    for (size_t i = 0; i < l->n; i++) {
        free(l->v[i].name);
        free(l->v[i].kind);
        free(l->v[i].scope);
    }
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

J *symbol_to_json(const Symbol *s) {
    J *o = j_obj();
    j_set(o, "name", j_str(s->name));
    j_set(o, "kind", j_str(s->kind));
    j_set(o, "start", j_int((int64_t)s->start));
    j_set(o, "end", j_int((int64_t)s->end));
    j_set(o, "line", j_int((int64_t)s->line));
    j_set(o, "column", j_int((int64_t)s->column));
    j_set(o, "scope", j_str(s->scope));
    return o;
}

/* ---- nested tagged types and fields ---- */
static void scan_nested(const Scan *sc, const Snapshot *snap, size_t i0, size_t i1, const char *scope,
                        bool fields, SymbolList *out);

static void scan_fields(const Scan *sc, const Snapshot *snap, size_t i0, size_t i1, const char *scope,
                        SymbolList *out) {
    size_t a = i0;
    while (a < i1) {
        size_t k = a;
        while (k < i1 && !punct(sc, k, ';')) {
            if (opener(sc, k)) k = match(sc, k);
            k++;
        }
        size_t b = k < i1 ? k : i1; /* index of ';' or end */
        if (b > a) {
            long nm = scope[0] ? declarator_name(sc, a, b) : -1;
            if (nm >= 0) {
                size_t bend = b < i1 ? sc->t[b].e : sc->t[b - 1].e;
                add_symbol(out, sc, snap, sc->src + sc->t[nm].s, sc->t[nm].e - sc->t[nm].s, "field",
                           sc->t[a].s, bend, scope);
            }
            scan_nested(sc, snap, a, b, scope, false, out);
        }
        a = k + 1;
    }
}

static void scan_nested(const Scan *sc, const Snapshot *snap, size_t i0, size_t i1, const char *scope,
                        bool fields, SymbolList *out) {
    (void)fields;
    size_t k = i0;
    while (k < i1) {
        if (sc->t[k].kind == T_ID && (tok_is(sc, k, "struct") || tok_is(sc, k, "union") || tok_is(sc, k, "enum"))) {
            const char *kind = tok_is(sc, k, "struct") ? "struct" : tok_is(sc, k, "union") ? "union" : "enum";
            size_t m = k + 1;
            while (m < i1 && sc->t[m].kind == T_ID && in_list(sc, m, GROUP_WORDS) && punct(sc, m + 1, '('))
                m = match(sc, m + 1) + 1;
            long tag = -1;
            if (m < i1 && is_ident(sc, m)) {
                tag = (long)m;
                m++;
            }
            while (m < i1 && sc->t[m].kind == T_ID && in_list(sc, m, GROUP_WORDS) && punct(sc, m + 1, '('))
                m = match(sc, m + 1) + 1;
            if (m < i1 && punct(sc, m, '{')) {
                size_t close = match(sc, m);
                char *region = NULL;
                if (tag >= 0) {
                    size_t tl = sc->t[tag].e - sc->t[tag].s;
                    add_symbol(out, sc, snap, sc->src + sc->t[tag].s, tl, kind, sc->t[k].s, sc->t[close].e, scope);
                    Buf b;
                    buf_init(&b);
                    if (scope[0]) {
                        buf_puts(&b, scope);
                        buf_puts(&b, "::");
                    }
                    buf_put(&b, sc->src + sc->t[tag].s, tl);
                    region = buf_take(&b);
                }
                const char *inner = region ? region : scope;
                if (kind[0] == 'e') scan_nested(sc, snap, m + 1, close, inner, false, out);
                else scan_fields(sc, snap, m + 1, close, inner, out);
                free(region);
                k = close + 1;
                continue;
            }
        }
        k++;
    }
}

/* ---- top-level outline ---- */
int c23_outline(const Snapshot *snap, bool nested, SymbolList *out) {
    memset(out, 0, sizeof *out);
    Scan sc;
    memset(&sc, 0, sizeof sc);
    sc.src = snap->text;
    sc.len = snap->len;
    size_t blocks = snap->len / CHECK + 2;
    sc.cp_check = na_alloc(blocks * sizeof *sc.cp_check);
    size_t cp = 0;
    for (size_t i = 0; i < snap->len; i++) {
        if (i % CHECK == 0) sc.cp_check[i / CHECK] = cp;
        if (((unsigned char)snap->text[i] & 0xC0) != 0x80) cp++;
    }
    sc.cp_check[snap->len / CHECK] = sc.cp_check[snap->len / CHECK]; /* tail block already set or 0 */
    if (snap->len % CHECK == 0) sc.cp_check[snap->len / CHECK] = cp;
    tokenize(&sc);

    size_t i = 0;
    while (i < sc.n) {
        if (punct(&sc, i, ';')) {
            i++;
            continue;
        }
        size_t start = i, j = i;
        bool is_typedef = tok_is(&sc, start, "typedef");
        bool seen_eq = false, is_func = false;
        size_t end = 0; /* index of the last token of the item */
        bool terminated = false;
        while (j < sc.n) {
            if (punct(&sc, j, '=')) seen_eq = true;
            if (punct(&sc, j, '{')) {
                size_t close = match(&sc, j);
                if (!seen_eq && !is_typedef && j > start && punct(&sc, j - 1, ')')) {
                    is_func = true;
                    end = close;
                    terminated = true;
                    break;
                }
                j = close + 1;
                continue;
            }
            if (opener(&sc, j)) {
                j = match(&sc, j) + 1;
                continue;
            }
            if (punct(&sc, j, ';')) {
                end = j;
                terminated = true;
                break;
            }
            j++;
        }
        if (!terminated) break;
        const char *kind = is_func ? "function" : is_typedef ? "typedef" : "declaration";
        size_t name_end = is_func ? end : end;
        long nm = declarator_name(&sc, start, name_end);
        if (nm >= 0)
            add_symbol(out, &sc, snap, sc.src + sc.t[nm].s, sc.t[nm].e - sc.t[nm].s, kind, sc.t[start].s,
                       sc.t[end].e, "");
        i = end + 1;
    }
    if (nested) scan_nested(&sc, snap, 0, sc.n, "", false, out);
    free(sc.t);
    free(sc.cp_check);
    return 0;
}

int c23_select(const Snapshot *snap, const char *name, const char *kind, const char *scope, Symbol *out) {
    SymbolList all;
    c23_outline(snap, true, &all);
    long found = -1;
    size_t count = 0;
    for (size_t i = 0; i < all.n; i++) {
        const Symbol *s = &all.v[i];
        if (strcmp(s->name, name) != 0) continue;
        if (kind && strcmp(s->kind, kind) != 0) continue;
        if (scope && strcmp(s->scope, scope) != 0) continue;
        if (count == 0) found = (long)i;
        count++;
    }
    int rc = 0;
    if (count == 0) {
        rc = na_fail("symbol_not_found: %s", name);
    } else if (count != 1) {
        rc = na_fail("ambiguous_symbol: %s (%zu matches)", name, count);
    } else {
        Symbol *s = &all.v[found];
        *out = *s;
        out->name = na_strdup(s->name);
        out->kind = na_strdup(s->kind);
        out->scope = na_strdup(s->scope);
    }
    symbols_free(&all);
    return rc;
}

int c23_replace_symbol(const Snapshot *snap, const char *name, const char *replacement, size_t rlen,
                       const char *kind, const char *scope, ChangeSet *out) {
    Symbol s;
    if (c23_select(snap, name, kind, scope, &s) != 0) return -1;
    cs_init(out);
    cs_add(out, s.start, s.end, replacement, rlen);
    free(s.name);
    free(s.kind);
    free(s.scope);
    return 0;
}
