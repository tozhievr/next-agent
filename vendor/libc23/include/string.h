/* <string.h> - C23 7.26 (libc23) */
#ifndef _LIBC23_STRING_H
#define _LIBC23_STRING_H

#define __need_size_t
#define __need_NULL
#include <stddef.h>

#define __STDC_VERSION_STRING_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *restrict, const void *restrict, size_t);
void *memccpy(void *restrict, const void *restrict, int, size_t);
void *memmove(void *, const void *, size_t);
char *strcpy(char *restrict, const char *restrict);
char *strncpy(char *restrict, const char *restrict, size_t);
char *strdup(const char *);
char *strndup(const char *, size_t);
char *strcat(char *restrict, const char *restrict);
char *strncat(char *restrict, const char *restrict, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int strcoll(const char *, const char *);
int strncmp(const char *, const char *, size_t);
size_t strxfrm(char *restrict, const char *restrict, size_t);
void *memchr(const void *, int, size_t);
char *strchr(const char *, int);
size_t strcspn(const char *, const char *);
char *strpbrk(const char *, const char *);
char *strrchr(const char *, int);
size_t strspn(const char *, const char *);
char *strstr(const char *, const char *);
char *strtok(char *restrict, const char *restrict);
void *memset(void *, int, size_t);
void *memset_explicit(void *, int, size_t);
char *strerror(int);
size_t strlen(const char *);

#ifdef __cplusplus
}
#endif

#if !defined(__cplusplus) && !defined(_LIBC23_BUILD) && !defined(_LIBC23_NO_QUAL_GENERIC)
/* C23 type-generic forms preserve const qualification in the returned pointer.
 * The non-null pointer operand forces conditional pointer composition, while
 * the controlling expression and unselected generic associations are not
 * evaluated. */
#define __LIBC23_MEMCHR_QUAL(p, call) \
    _Generic((1 ? (p) : (void *)1), \
        const void *: (const void *)(call), \
        default: (void *)(call))
#define __LIBC23_STRING_QUAL(s, call) \
    _Generic((1 ? (s) : (char *)1), \
        const char *: (const char *)(call), \
        default: (char *)(call))
#define memchr(p, c, n) \
    __LIBC23_MEMCHR_QUAL((p), (memchr)((p), (c), (n)))
#define strchr(s, c) \
    __LIBC23_STRING_QUAL((s), (strchr)((s), (c)))
#define strpbrk(s, accept) \
    __LIBC23_STRING_QUAL((s), (strpbrk)((s), (accept)))
#define strrchr(s, c) \
    __LIBC23_STRING_QUAL((s), (strrchr)((s), (c)))
#define strstr(s, find) \
    __LIBC23_STRING_QUAL((s), (strstr)((s), (find)))
#endif
#endif
