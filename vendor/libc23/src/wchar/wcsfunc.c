/* <wchar.h> string and array functions */
#include <wchar.h>
#include "libc.h"

wchar_t *wcscpy(wchar_t *restrict d, const wchar_t *restrict s) {
    wchar_t *r = d;
    while ((*d++ = *s++)) {}
    return r;
}

wchar_t *wcsncpy(wchar_t *restrict d, const wchar_t *restrict s, size_t n) {
    wchar_t *r = d;
    for (; n && *s; n--) *d++ = *s++;
    for (; n; n--) *d++ = 0;
    return r;
}

wchar_t *wmemcpy(wchar_t *restrict d, const wchar_t *restrict s, size_t n) {
    wchar_t *r = d;
    while (n--) *d++ = *s++;
    return r;
}

wchar_t *wmemmove(wchar_t *d, const wchar_t *s, size_t n) {
    wchar_t *r = d;
    if (d == s || !n) return d;
    if ((uintptr_t)d - (uintptr_t)s >= n * sizeof(wchar_t)) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return r;
}

wchar_t *wcscat(wchar_t *restrict d, const wchar_t *restrict s) {
    wcscpy(d + wcslen(d), s);
    return d;
}

wchar_t *wcsncat(wchar_t *restrict d, const wchar_t *restrict s, size_t n) {
    wchar_t *r = d;
    d += wcslen(d);
    for (; n && *s; n--) *d++ = *s++;
    *d = 0;
    return r;
}

/* wide characters are compared as unsigned scalar values (code point order) */
static inline int cmpwc(wchar_t a, wchar_t b) {
    uint32_t x = (uint32_t)a, y = (uint32_t)b;
    return x < y ? -1 : x > y;
}

int wcscmp(const wchar_t *a, const wchar_t *b) {
    for (; *a && *a == *b; a++, b++) {}
    return cmpwc(*a, *b);
}

int wcscoll(const wchar_t *a, const wchar_t *b) { return wcscmp(a, b); }

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) {
    for (; n && *a && *a == *b; n--, a++, b++) {}
    return n ? cmpwc(*a, *b) : 0;
}

size_t wcsxfrm(wchar_t *restrict d, const wchar_t *restrict s, size_t n) {
    size_t l = wcslen(s);
    if (n) {
        size_t c = l < n ? l : n - 1;
        for (size_t i = 0; i < c; i++) d[i] = s[i];
        d[c] = 0;
    }
    return l;
}

int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n) {
    for (; n; n--, a++, b++)
        if (*a != *b) return cmpwc(*a, *b);
    return 0;
}

wchar_t *(wcschr)(const wchar_t *s, wchar_t c) {
    for (;; s++) {
        if (*s == c) return (wchar_t *)s;
        if (!*s) return NULL;
    }
}

wchar_t *(wcsrchr)(const wchar_t *s, wchar_t c) {
    const wchar_t *r = NULL;
    for (;; s++) {
        if (*s == c) r = s;
        if (!*s) return (wchar_t *)r;
    }
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject) {
    const wchar_t *p = s;
    for (; *p; p++)
        for (const wchar_t *r = reject; *r; r++)
            if (*r == *p) return (size_t)(p - s);
    return (size_t)(p - s);
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept) {
    const wchar_t *p = s;
    for (; *p; p++) {
        const wchar_t *a = accept;
        while (*a && *a != *p) a++;
        if (!*a) break;
    }
    return (size_t)(p - s);
}

wchar_t *(wcspbrk)(const wchar_t *s, const wchar_t *accept) {
    s += wcscspn(s, accept);
    return *s ? (wchar_t *)s : NULL;
}

wchar_t *(wcsstr)(const wchar_t *h, const wchar_t *n) {
    if (!*n) return (wchar_t *)h;
    for (; *h; h++) {
        if (*h != *n) continue;
        const wchar_t *a = h, *b = n;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return (wchar_t *)h;
        if (!*a) return NULL;
    }
    return NULL;
}

size_t wcslen(const wchar_t *s) {
    const wchar_t *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

wchar_t *(wmemchr)(const wchar_t *s, wchar_t c, size_t n) {
    for (; n; n--, s++)
        if (*s == c) return (wchar_t *)s;
    return NULL;
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
    wchar_t *r = s;
    while (n--) *s++ = c;
    return r;
}

wchar_t *wcstok(wchar_t *restrict s, const wchar_t *restrict delim, wchar_t **restrict save) {
    if (!s && !(s = *save)) return NULL;
    s += wcsspn(s, delim);
    if (!*s) { *save = NULL; return NULL; }
    wchar_t *tok = s;
    s += wcscspn(s, delim);
    if (*s) { *s = 0; *save = s + 1; }
    else *save = NULL;
    return tok;
}
