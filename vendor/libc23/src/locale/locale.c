/* setlocale / localeconv.  Supported: "C"(="POSIX") and "C.UTF-8"(="C.utf8"). */
#include <locale.h>
#include <limits.h>
#include "libc.h"

int __libc_locale_utf8;

enum { LOC_C = 0, LOC_UTF8 = 1 };
#define NCAT 5 /* LC_CTYPE, NUMERIC, TIME, COLLATE, MONETARY (0..4) */

static const char *const loc_names[2] = { "C", "C.UTF-8" };
static const char *const cat_names[NCAT] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY"
};
static int cat_state[NCAT]; /* all LOC_C initially */
static char composite[128];

static struct lconv lc = {
    ".", "", "", "", "", "", "", "", "",
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    "",
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX
};

void __libc_locale_init(void) {
    for (int i = 0; i < NCAT; i++) cat_state[i] = LOC_C;
    __libc_locale_utf8 = 0;
}

size_t __ctype_get_mb_cur_max(void) { return __libc_locale_utf8 ? 4 : 1; }

struct lconv *localeconv(void) { return &lc; }

static int eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int eqi(const char *a, const char *b) {
    while (*a && lower(*a) == lower(*b)) a++, b++;
    return *a == *b;
}
/* Name -> state, or -1 if unsupported. */
static int lookup(const char *n) {
    if (eq(n, "C") || eq(n, "POSIX")) return LOC_C;
    if (eqi(n, "C.UTF-8") || eqi(n, "C.utf8")) return LOC_UTF8;
    return -1;
}

static const char *env(const char *name) {
    char **e = pal_environ;
    if (!e) return NULL;
    size_t n = 0;
    while (name[n]) n++;
    for (; *e; e++) {
        size_t i = 0;
        while (i < n && (*e)[i] == name[i]) i++;
        if (i == n && (*e)[n] == '=') return (*e) + n + 1;
    }
    return NULL;
}

/* Locale from the environment for one category (setlocale(cat, "")). */
static int from_env(int cat) {
    const char *v = env("LC_ALL");
    if (!v || !*v) v = env(cat_names[cat]);
    if (!v || !*v) v = env("LANG");
    if (!v || !*v) return LOC_UTF8; /* nothing set: libc23 strings are UTF-8 */
    return lookup(v);
}

static const char *current(int cat) {
    if (cat != LC_ALL) return loc_names[cat_state[cat]];
    int same = 1;
    for (int i = 1; i < NCAT; i++) if (cat_state[i] != cat_state[0]) same = 0;
    if (same) return loc_names[cat_state[0]];
    char *p = composite;
    for (int i = 0; i < NCAT; i++) {
        for (const char *s = cat_names[i]; *s; ) *p++ = *s++;
        *p++ = '=';
        for (const char *s = loc_names[cat_state[i]]; *s; ) *p++ = *s++;
        *p++ = ';';
    }
    p[-1] = 0;
    return composite;
}

char *setlocale(int cat, const char *name) {
    if (cat != LC_ALL && (cat < 0 || cat >= NCAT)) return NULL;
    if (!name) return (char *)current(cat);

    int next[NCAT];
    for (int i = 0; i < NCAT; i++) next[i] = cat_state[i];

    if (*name == 0) {
        for (int i = 0; i < NCAT; i++) {
            if (cat != LC_ALL && cat != i) continue;
            int s = from_env(i);
            if (s < 0) return NULL;
            next[i] = s;
        }
    } else if (cat == LC_ALL) {
        const char *p = name;
        int has_eq = 0;
        for (const char *s = name; *s; s++) if (*s == '=') has_eq = 1;
        if (!has_eq) {
            int s = lookup(name);
            if (s < 0) return NULL;
            for (int i = 0; i < NCAT; i++) next[i] = s;
        } else {
            /* "LC_CTYPE=C.UTF-8;LC_NUMERIC=C;..." */
            unsigned seen = 0;
            while (*p) {
                int found = -1;
                for (int i = 0; i < NCAT; i++) {
                    const char *k = cat_names[i], *q = p;
                    while (*k && *k == *q) k++, q++;
                    if (!*k && *q == '=') { found = i; p = q + 1; break; }
                }
                if (found < 0) return NULL;
                unsigned bit = 1u << (unsigned)found;
                if (seen & bit) return NULL;
                seen |= bit;
                char buf[16]; int n = 0;
                while (*p && *p != ';') { if (n >= 15) return NULL; buf[n++] = *p++; }
                buf[n] = 0;
                int s = lookup(buf);
                if (s < 0) return NULL;
                next[found] = s;
                if (*p == ';') {
                    p++;
                    if (!*p) return NULL;
                }
            }
            if (seen != (1u << NCAT) - 1u) return NULL;
        }
    } else {
        int s = lookup(name);
        if (s < 0) return NULL;
        next[cat] = s;
    }
    for (int i = 0; i < NCAT; i++) cat_state[i] = next[i];
    __libc_locale_utf8 = cat_state[LC_CTYPE] == LOC_UTF8;
    return (char *)current(cat);
}
