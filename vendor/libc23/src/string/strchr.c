/* strchr, strrchr */
#include <string.h>

char *strchr(const char *s, int c) {
    unsigned char ch = (unsigned char)c;
    for (;; s++) {
        if ((unsigned char)*s == ch) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c) {
    unsigned char ch = (unsigned char)c;
    const char *r = NULL;
    for (;; s++) {
        if ((unsigned char)*s == ch) r = s;
        if (!*s) return (char *)r;
    }
}
