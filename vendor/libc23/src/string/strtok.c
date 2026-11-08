#include <string.h>
#include "libc.h"

char *strtok(char *restrict s, const char *restrict delim) {
    struct libc_tls *t = libc_tls();
    if (!s && !(s = t->strtok_next)) return NULL;
    s += strspn(s, delim);
    if (!*s) { t->strtok_next = NULL; return NULL; }
    char *tok = s;
    s += strcspn(s, delim);
    if (*s) { *s++ = 0; t->strtok_next = s; }
    else t->strtok_next = NULL;
    return tok;
}
