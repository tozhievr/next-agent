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

static void tokenize(Scan *sc, size_t from, bool line_start) {
    const char *s = sc->src;
    size_t len = sc->len, i = from;
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

/* ---- symbols in byte coordinates (cacheable) ---- */
typedef struct {
    char *name, *kind, *scope;
    size_t bs, be;
} BSym;

typedef struct {
    BSym *v;
    size_t n, cap;
} BList;

typedef struct {
    size_t bs, be; /* first token start .. last token end (tail: to end of text) */
    bool has_top, tail;
    BSym top;
    BSym *nested;
    size_t nn;
} Item;

typedef struct {
    Item *v;
    size_t n, cap;
} ItemList;

struct OutlineCache {
    char *text;
    size_t len;
    ItemList items;
    bool built;
    size_t last_tokens; /* tokens scanned by the last update (diagnostics/tests) */
    size_t last_reused; /* items reused by the last update */
};

static void bsym_free(BSym *s) {
    free(s->name);
    free(s->kind);
    free(s->scope);
}

static void item_free(Item *it) {
    if (it->has_top) bsym_free(&it->top);
    for (size_t i = 0; i < it->nn; i++) bsym_free(&it->nested[i]);
    free(it->nested);
}

static void add_bsym(BList *out, const char *name, size_t name_len, const char *kind, size_t bs, size_t be,
                     const char *scope) {
    if (out->n == out->cap) {
        out->cap = out->cap ? out->cap * 2 : 8;
        out->v = na_realloc(out->v, out->cap * sizeof *out->v);
    }
    BSym *s = &out->v[out->n++];
    s->name = na_strndup(name, name_len);
    s->kind = na_strdup(kind);
    s->scope = na_strdup(scope);
    s->bs = bs;
    s->be = be;
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
static void scan_nested(const Scan *sc, size_t i0, size_t i1, const char *scope, BList *out);

static void scan_fields(const Scan *sc, size_t i0, size_t i1, const char *scope, BList *out) {
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
                add_bsym(out, sc->src + sc->t[nm].s, sc->t[nm].e - sc->t[nm].s, "field", sc->t[a].s, bend, scope);
            }
            scan_nested(sc, a, b, scope, out);
        }
        a = k + 1;
    }
}

static void scan_nested(const Scan *sc, size_t i0, size_t i1, const char *scope, BList *out) {
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
                    add_bsym(out, sc->src + sc->t[tag].s, tl, kind, sc->t[k].s, sc->t[close].e, scope);
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
                if (kind[0] == 'e') scan_nested(sc, m + 1, close, inner, out);
                else scan_fields(sc, m + 1, close, inner, out);
                free(region);
                k = close + 1;
                continue;
            }
        }
        k++;
    }
}

/* True when the opener at `i` has a matching closer in the stream (match() falls back to the last token). */
static bool balanced_at(const Scan *sc, size_t i) {
    int depth = 0;
    for (size_t k = i; k < sc->n; k++) {
        if (sc->t[k].kind != T_PUNCT) continue;
        char c = sc->t[k].ch;
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (depth <= 0) return true;
        }
    }
    return false;
}

/* ---- top-level items ---- */
typedef struct {
    const Item *old;
    size_t nold, m;      /* candidate old item */
    ptrdiff_t delta;     /* new length - old length */
    size_t old_edit_end; /* end of the changed region, old coordinates */
    size_t new_edit_end; /* ... new coordinates */
    bool active;
    size_t hit; /* old item index at which parsing resynchronised */
} Resync;

static void item_push(ItemList *l, Item it) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->v = na_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = it;
}

static void item_take_nested(Item *it, const Scan *sc, size_t i0, size_t i1) {
    BList bl;
    memset(&bl, 0, sizeof bl);
    scan_nested(sc, i0, i1, "", &bl);
    it->nested = bl.v;
    it->nn = bl.n;
}

/* Parses items from the tokens of `sc`. Returns 1 when the whole input was consumed (`complete`),
 * 0 when more text is needed, 2 on resynchronisation with the old item list (rs->hit). */
static int parse_items(const Scan *sc, bool complete, size_t text_len, ItemList *out, Resync *rs) {
    size_t i = 0;
    while (i < sc->n) {
        if (punct(sc, i, ';')) {
            i++;
            continue;
        }
        size_t start = i, j = i;
        bool is_typedef = tok_is(sc, start, "typedef");
        bool seen_eq = false, is_func = false;
        size_t end = 0;
        bool terminated = false, open_body = false;
        while (j < sc->n) {
            if (punct(sc, j, '=')) seen_eq = true;
            if (punct(sc, j, '{')) {
                size_t close = match(sc, j);
                if (!seen_eq && !is_typedef && j > start && punct(sc, j - 1, ')')) {
                    is_func = true;
                    end = close;
                    terminated = true;
                    /* An unclosed body ends at the last token of the stream: its extent depends on
                     * everything that follows, so it behaves like the tail and is never reused. */
                    open_body = close == sc->n - 1 && !balanced_at(sc, j);
                    break;
                }
                j = close + 1;
                continue;
            }
            if (opener(sc, j)) {
                j = match(sc, j) + 1;
                continue;
            }
            if (punct(sc, j, ';')) {
                end = j;
                terminated = true;
                break;
            }
            j++;
        }
        /* An item is only trusted when a later token proves its tokens are complete. */
        if (!terminated || (!complete && end + 1 >= sc->n)) {
            if (!complete) return 0;
            Item it;
            memset(&it, 0, sizeof it);
            it.tail = true;
            it.bs = sc->t[start].s;
            it.be = text_len;
            item_take_nested(&it, sc, start, sc->n);
            item_push(out, it);
            return 1;
        }
        const char *kind = is_func ? "function" : is_typedef ? "typedef" : "declaration";
        long nm = declarator_name(sc, start, end);
        Item it;
        memset(&it, 0, sizeof it);
        it.bs = sc->t[start].s;
        it.be = sc->t[end].e;
        if (nm >= 0) {
            BList one;
            memset(&one, 0, sizeof one);
            add_bsym(&one, sc->src + sc->t[nm].s, sc->t[nm].e - sc->t[nm].s, kind, it.bs, it.be, "");
            it.top = one.v[0];
            it.has_top = true;
            free(one.v);
        }
        item_take_nested(&it, sc, start, end + 1);
        if (open_body) {
            it.tail = true;
            it.be = text_len;
            item_push(out, it);
            return 1;
        }
        item_push(out, it);
        i = end + 1;
        if (rs && rs->active && it.be >= rs->new_edit_end) {
            while (rs->m < rs->nold && !rs->old[rs->m].tail && (ptrdiff_t)rs->old[rs->m].be + rs->delta < (ptrdiff_t)it.be)
                rs->m++;
            if (rs->m < rs->nold && !rs->old[rs->m].tail && rs->old[rs->m].be >= rs->old_edit_end &&
                (ptrdiff_t)rs->old[rs->m].be + rs->delta == (ptrdiff_t)it.be) {
                rs->hit = rs->m;
                return 2;
            }
        }
    }
    return complete ? 1 : 0;
}

static void scan_window(Scan *sc, const char *text, size_t from, size_t to, bool line_start) {
    memset(sc, 0, sizeof *sc);
    sc->src = text;
    sc->len = to;
    tokenize(sc, from, line_start);
}

/* ---- cache ---- */
OutlineCache *outline_cache_new(void) {
    OutlineCache *c = na_alloc(sizeof *c);
    memset(c, 0, sizeof *c);
    return c;
}

void outline_cache_free(OutlineCache *c) {
    if (!c) return;
    for (size_t i = 0; i < c->items.n; i++) item_free(&c->items.v[i]);
    free(c->items.v);
    free(c->text);
    free(c);
}

size_t outline_cache_last_tokens(const OutlineCache *c) { return c->last_tokens; }
size_t outline_cache_last_reused(const OutlineCache *c) { return c->last_reused; }

static void shift_sym(BSym *s, ptrdiff_t d) {
    s->bs = (size_t)((ptrdiff_t)s->bs + d);
    s->be = (size_t)((ptrdiff_t)s->be + d);
}

static void shift_item(Item *it, ptrdiff_t d) {
    it->bs = (size_t)((ptrdiff_t)it->bs + d);
    it->be = (size_t)((ptrdiff_t)it->be + d);
    if (it->has_top) shift_sym(&it->top, d);
    for (size_t i = 0; i < it->nn; i++) shift_sym(&it->nested[i], d);
}

#define WINDOW_SLACK 4096

static void cache_store_text(OutlineCache *c, const Snapshot *snap) {
    free(c->text);
    c->text = na_strndup(snap->text, snap->len);
    c->len = snap->len;
}

static void cache_rebuild(OutlineCache *c, const Snapshot *snap) {
    for (size_t i = 0; i < c->items.n; i++) item_free(&c->items.v[i]);
    c->items.n = 0;
    Scan sc;
    scan_window(&sc, snap->text, 0, snap->len, true);
    parse_items(&sc, true, snap->len, &c->items, NULL);
    c->last_tokens = sc.n;
    c->last_reused = 0;
    free(sc.t);
    cache_store_text(c, snap);
    c->built = true;
}

/* Brings the cache up to date with `snap`: re-lexes only the window around the change and splices
 * the result with the unchanged items before and after it. */
static void cache_update(OutlineCache *c, const Snapshot *snap) {
    if (!c->built) {
        cache_rebuild(c, snap);
        return;
    }
    size_t olen = c->len, nlen = snap->len;
    size_t p = 0, minlen = olen < nlen ? olen : nlen;
    while (p < minlen && c->text[p] == snap->text[p]) p++;
    if (p == olen && p == nlen) {
        c->last_tokens = 0;
        c->last_reused = c->items.n;
        return;
    }
    size_t s = 0;
    while (s < minlen - p && c->text[olen - 1 - s] == snap->text[nlen - 1 - s]) s++;
    size_t old_edit_end = olen - s, new_edit_end = nlen - s;
    ptrdiff_t delta = (ptrdiff_t)nlen - (ptrdiff_t)olen;

    /* First old item that could be affected; restart after the item before it. */
    size_t r = 0;
    while (r < c->items.n && c->items.v[r].be < p) r++;
    size_t restart = r > 0 ? c->items.v[r - 1].be : 0;
    bool line_start = r == 0;

    size_t window = (new_edit_end > restart ? new_edit_end - restart : 0) + WINDOW_SLACK;
    ItemList fresh;
    Resync rs;
    int res;
    size_t tokens = 0;
    for (;;) {
        size_t to = restart + window;
        bool complete = to >= nlen;
        if (complete) to = nlen;
        memset(&fresh, 0, sizeof fresh);
        memset(&rs, 0, sizeof rs);
        rs.old = c->items.v;
        rs.nold = c->items.n;
        rs.m = r;
        rs.delta = delta;
        rs.old_edit_end = old_edit_end;
        rs.new_edit_end = new_edit_end;
        rs.active = true;
        Scan sc;
        scan_window(&sc, snap->text, restart, to, line_start);
        tokens += sc.n;
        res = parse_items(&sc, complete, nlen, &fresh, &rs);
        free(sc.t);
        if (res != 0) break;
        for (size_t i = 0; i < fresh.n; i++) item_free(&fresh.v[i]);
        free(fresh.v);
        window *= 2;
    }

    ItemList merged;
    memset(&merged, 0, sizeof merged);
    for (size_t i = 0; i < r; i++) item_push(&merged, c->items.v[i]);
    for (size_t i = 0; i < fresh.n; i++) item_push(&merged, fresh.v[i]);
    free(fresh.v);
    size_t keep_from = res == 2 ? rs.hit + 1 : c->items.n;
    for (size_t i = r; i < keep_from && i < c->items.n; i++) item_free(&c->items.v[i]);
    for (size_t i = keep_from; i < c->items.n; i++) {
        shift_item(&c->items.v[i], delta);
        item_push(&merged, c->items.v[i]);
    }
    c->last_reused = r + (c->items.n > keep_from ? c->items.n - keep_from : 0);
    free(c->items.v);
    c->items = merged;
    c->last_tokens = tokens;
    cache_store_text(c, snap);
}

/* ---- byte -> codepoint/line conversion ---- */
typedef struct {
    const Snapshot *snap;
    size_t *cp_check; /* codepoint count at byte multiples of CHECK */
} CpMap;

static void cpmap_init(CpMap *m, const Snapshot *snap) {
    m->snap = snap;
    size_t blocks = snap->len / CHECK + 2;
    m->cp_check = na_alloc(blocks * sizeof *m->cp_check);
    size_t cp = 0;
    for (size_t i = 0; i < snap->len; i++) {
        if (i % CHECK == 0) m->cp_check[i / CHECK] = cp;
        if (((unsigned char)snap->text[i] & 0xC0) != 0x80) cp++;
    }
    if (snap->len % CHECK == 0) m->cp_check[snap->len / CHECK] = cp;
}

static size_t cpmap_cp(const CpMap *m, size_t b) {
    size_t blk = b / CHECK;
    size_t cp = m->cp_check[blk];
    for (size_t i = blk * CHECK; i < b; i++)
        if (((unsigned char)m->snap->text[i] & 0xC0) != 0x80) cp++;
    return cp;
}

static void emit_symbol(SymbolList *out, const CpMap *m, const BSym *b) {
    if (out->n == out->cap) {
        out->cap = out->cap ? out->cap * 2 : 16;
        out->v = na_realloc(out->v, out->cap * sizeof *out->v);
    }
    Symbol *s = &out->v[out->n++];
    s->name = na_strdup(b->name);
    s->kind = na_strdup(b->kind);
    s->scope = na_strdup(b->scope);
    s->start = cpmap_cp(m, b->bs);
    s->end = cpmap_cp(m, b->be);
    size_t line = 1, col = 0;
    snap_position(m->snap, s->start, &line, &col);
    s->line = line;
    s->column = col;
}

static void cache_emit(const OutlineCache *c, const Snapshot *snap, bool nested, SymbolList *out) {
    memset(out, 0, sizeof *out);
    CpMap m;
    cpmap_init(&m, snap);
    for (size_t i = 0; i < c->items.n; i++)
        if (c->items.v[i].has_top) emit_symbol(out, &m, &c->items.v[i].top);
    if (nested)
        for (size_t i = 0; i < c->items.n; i++)
            for (size_t k = 0; k < c->items.v[i].nn; k++) emit_symbol(out, &m, &c->items.v[i].nested[k]);
    free(m.cp_check);
}

/* ---- public API ---- */
int c23_outline_cached(OutlineCache *c, const Snapshot *snap, bool nested, SymbolList *out) {
    cache_update(c, snap);
    cache_emit(c, snap, nested, out);
    return 0;
}

int c23_outline(const Snapshot *snap, bool nested, SymbolList *out) {
    OutlineCache *c = outline_cache_new();
    int rc = c23_outline_cached(c, snap, nested, out);
    outline_cache_free(c);
    return rc;
}

int symbols_select(const SymbolList *all, const char *name, const char *kind, const char *scope, Symbol *out) {
    long found = -1;
    size_t count = 0;
    for (size_t i = 0; i < all->n; i++) {
        const Symbol *s = &all->v[i];
        if (strcmp(s->name, name) != 0) continue;
        if (kind && strcmp(s->kind, kind) != 0) continue;
        if (scope && strcmp(s->scope, scope) != 0) continue;
        if (count == 0) found = (long)i;
        count++;
    }
    if (count == 0) return na_fail("symbol_not_found: %s", name);
    if (count != 1) return na_fail("ambiguous_symbol: %s (%zu matches)", name, count);
    const Symbol *s = &all->v[found];
    *out = *s;
    out->name = na_strdup(s->name);
    out->kind = na_strdup(s->kind);
    out->scope = na_strdup(s->scope);
    return 0;
}

int c23_select_cached(OutlineCache *c, const Snapshot *snap, const char *name, const char *kind, const char *scope,
                      Symbol *out) {
    SymbolList all;
    c23_outline_cached(c, snap, true, &all);
    int rc = symbols_select(&all, name, kind, scope, out);
    symbols_free(&all);
    return rc;
}

int c23_select(const Snapshot *snap, const char *name, const char *kind, const char *scope, Symbol *out) {
    OutlineCache *c = outline_cache_new();
    int rc = c23_select_cached(c, snap, name, kind, scope, out);
    outline_cache_free(c);
    return rc;
}

int c23_replace_symbol_cached(OutlineCache *c, const Snapshot *snap, const char *name, const char *replacement,
                              size_t rlen, const char *kind, const char *scope, ChangeSet *out) {
    Symbol s;
    if (c23_select_cached(c, snap, name, kind, scope, &s) != 0) return -1;
    cs_init(out);
    cs_add(out, s.start, s.end, replacement, rlen);
    free(s.name);
    free(s.kind);
    free(s.scope);
    return 0;
}

int c23_replace_symbol(const Snapshot *snap, const char *name, const char *replacement, size_t rlen,
                       const char *kind, const char *scope, ChangeSet *out) {
    OutlineCache *c = outline_cache_new();
    int rc = c23_replace_symbol_cached(c, snap, name, replacement, rlen, kind, scope, out);
    outline_cache_free(c);
    return rc;
}

/* Test hook: index of the first item that differs from a from-scratch parse of `snap`, or
 * (size_t)-1 when the cache is exactly what a full scan would build. */
size_t outline_cache_first_divergence(const OutlineCache *c, const Snapshot *snap) {
    OutlineCache *f = outline_cache_new();
    cache_rebuild(f, snap);
    size_t k = 0, rc = (size_t)-1;
    for (; k < c->items.n && k < f->items.n; k++) {
        const Item *a = &c->items.v[k], *b = &f->items.v[k];
        if (a->bs != b->bs || a->be != b->be || a->tail != b->tail || a->has_top != b->has_top || a->nn != b->nn ||
            (a->has_top && (strcmp(a->top.name, b->top.name) || a->top.bs != b->top.bs || a->top.be != b->top.be))) {
            rc = k;
            break;
        }
    }
    if (rc == (size_t)-1 && c->items.n != f->items.n) rc = k;
    outline_cache_free(f);
    return rc;
}
