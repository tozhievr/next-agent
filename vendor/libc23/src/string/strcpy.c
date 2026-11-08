/* strcpy, strncpy, strcat, strncat, strdup, strndup */
#include <string.h>

void *malloc(size_t);

char *strcpy(char *restrict d, const char *restrict s) {
    memcpy(d, s, strlen(s) + 1);
    return d;
}

char *strncpy(char *restrict d, const char *restrict s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    if (i < n) memset(d + i, 0, n - i);
    return d;
}

char *strcat(char *restrict d, const char *restrict s) {
    memcpy(d + strlen(d), s, strlen(s) + 1);
    return d;
}

char *strncat(char *restrict d, const char *restrict s, size_t n) {
    char *e = d + strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; i++) e[i] = s[i];
    e[i] = 0;
    return d;
}

char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    return p ? memcpy(p, s, n) : NULL;
}

char *strndup(const char *s, size_t n) {
    size_t l = 0;
    while (l < n && s[l]) l++;
    char *p = malloc(l + 1);
    if (!p) return NULL;
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}
