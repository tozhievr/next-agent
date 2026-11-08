/* <wctype.h>: ASCII in locale "C", full Unicode in "C.UTF-8". */
#include <wctype.h>
#include <stdint.h>
#include "libc.h"
#include "../unicode/unidata.h"

enum { W_ALNUM = 1, W_ALPHA, W_BLANK, W_CNTRL, W_DIGIT, W_GRAPH, W_LOWER, W_PRINT, W_PUNCT, W_SPACE, W_UPPER, W_XDIGIT };

static int ascii_is(int kind, unsigned c) {
    int up = c - 'A' < 26u, lo = c - 'a' < 26u, dg = c - '0' < 10u;
    switch (kind) {
    case W_ALNUM: return up || lo || dg;
    case W_ALPHA: return up || lo;
    case W_BLANK: return c == ' ' || c == '\t';
    case W_CNTRL: return c < 0x20 || c == 0x7F;
    case W_DIGIT: return dg;
    case W_GRAPH: return c - 0x21u < 0x5Eu;
    case W_LOWER: return lo;
    case W_PRINT: return c - 0x20u < 0x5Fu;
    case W_PUNCT: return c - 0x21u < 0x5Eu && !(up || lo || dg);
    case W_SPACE: return c == ' ' || c - 9u < 5u;
    case W_UPPER: return up;
    case W_XDIGIT: return dg || (c | 32) - 'a' < 6u;
    }
    return 0;
}

static int is(int kind, wint_t wc) {
    if (wc < 0x80) return ascii_is(kind, wc);
    if (!__libc_locale_utf8) return 0;
    unsigned f = __libc_uni_flags(wc);
    switch (kind) {
    case W_ALNUM: case W_ALPHA: return f & UNI_ALPHA;
    case W_BLANK: return f & UNI_BLANK;
    case W_CNTRL: return f & UNI_CNTRL;
    case W_DIGIT: case W_XDIGIT: return 0;
    case W_GRAPH: return f & UNI_GRAPH;
    case W_LOWER: return f & UNI_LOWER;
    case W_PRINT: return f & UNI_PRINT;
    case W_PUNCT: return f & UNI_PUNCT;
    case W_SPACE: return f & UNI_SPACE;
    case W_UPPER: return f & UNI_UPPER;
    }
    return 0;
}

int iswalnum(wint_t c)  { return is(W_ALNUM, c) != 0; }
int iswalpha(wint_t c)  { return is(W_ALPHA, c) != 0; }
int iswblank(wint_t c)  { return is(W_BLANK, c) != 0; }
int iswcntrl(wint_t c)  { return is(W_CNTRL, c) != 0; }
int iswdigit(wint_t c)  { return is(W_DIGIT, c) != 0; }
int iswgraph(wint_t c)  { return is(W_GRAPH, c) != 0; }
int iswlower(wint_t c)  { return is(W_LOWER, c) != 0; }
int iswprint(wint_t c)  { return is(W_PRINT, c) != 0; }
int iswpunct(wint_t c)  { return is(W_PUNCT, c) != 0; }
int iswspace(wint_t c)  { return is(W_SPACE, c) != 0; }
int iswupper(wint_t c)  { return is(W_UPPER, c) != 0; }
int iswxdigit(wint_t c) { return is(W_XDIGIT, c) != 0; }

static const char *const names[] = {
    "alnum", "alpha", "blank", "cntrl", "digit", "graph",
    "lower", "print", "punct", "space", "upper", "xdigit"
};

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

wctype_t wctype(const char *prop) {
    for (unsigned i = 0; i < LIBC_ARRAY_LEN(names); i++)
        if (streq(prop, names[i])) return i + 1;
    return 0;
}

int iswctype(wint_t c, wctype_t d) {
    if (d < 1 || d > LIBC_ARRAY_LEN(names)) return 0;
    return is((int)d, c) != 0;
}

wint_t towlower(wint_t c) {
    if (c - 'A' < 26u) return c + 32;
    if (c < 0x80 || !__libc_locale_utf8) return c;
    return __libc_uni_tolower(c);
}
wint_t towupper(wint_t c) {
    if (c - 'a' < 26u) return c - 32;
    if (c < 0x80 || !__libc_locale_utf8) return c;
    return __libc_uni_toupper(c);
}

wctrans_t wctrans(const char *prop) {
    if (streq(prop, "tolower")) return 1;
    if (streq(prop, "toupper")) return 2;
    return 0;
}
wint_t towctrans(wint_t c, wctrans_t d) {
    return d == 1 ? towlower(c) : d == 2 ? towupper(c) : c;
}
