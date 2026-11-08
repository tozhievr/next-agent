/* strcmp, strncmp, strcoll, strxfrm.  The supported locales ("C", "POSIX",
 * "C.UTF-8") collate by code unit value, so strcoll == strcmp. */
#include <string.h>

int strcmp(const char *a, const char *b) {
    const unsigned char *x = (const void *)a, *y = (const void *)b;
    while (*x && *x == *y) { x++; y++; }
    return *x < *y ? -1 : *x > *y;
}

int strncmp(const char *a, const char *b, size_t n) {
    const unsigned char *x = (const void *)a, *y = (const void *)b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x < *y ? -1 : 1;
        if (!*x) return 0;
    }
    return 0;
}

int strcoll(const char *a, const char *b) { return strcmp(a, b); }

size_t strxfrm(char *restrict d, const char *restrict s, size_t n) {
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}
