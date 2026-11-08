/* strspn, strcspn, strpbrk */
#include <string.h>

static void build_set(unsigned char set[32], const char *chars) {
    for (int i = 0; i < 32; i++) set[i] = 0;
    for (const unsigned char *p = (const void *)chars; *p; p++) set[*p >> 3] |= (unsigned char)(1u << (*p & 7));
}
#define IN(set, c) ((set)[(unsigned char)(c) >> 3] >> ((unsigned char)(c) & 7) & 1)

size_t strspn(const char *s, const char *accept) {
    const char *a = s;
    if (!accept[0]) return 0;
    if (!accept[1]) { while (*s == *accept) s++; return (size_t)(s - a); }
    unsigned char set[32]; build_set(set, accept);
    while (*s && IN(set, *s)) s++;
    return (size_t)(s - a);
}

size_t strcspn(const char *s, const char *reject) {
    const char *a = s;
    if (!reject[0]) return strlen(s);
    if (!reject[1]) { char c = *reject; while (*s && *s != c) s++; return (size_t)(s - a); }
    unsigned char set[32]; build_set(set, reject);
    while (*s && !IN(set, *s)) s++;
    return (size_t)(s - a);
}

char *strpbrk(const char *s, const char *accept) {
    s += strcspn(s, accept);
    return *s ? (char *)s : NULL;
}
