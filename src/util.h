/* NextAgent: basic utilities (buffers, errors, UTF-8, BLAKE2b, paths). */
#ifndef NA_UTIL_H
#define NA_UTIL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NA_VERSION "0.44.0"

/* Last-error message, set by failing functions (which return -1 / NULL). */
const char *na_errmsg(void);
void na_seterr(const char *fmt, ...);
int na_fail(const char *fmt, ...); /* sets the error and returns -1 */

void *na_alloc(size_t n); /* zero-filled; aborts on OOM */
void *na_realloc(void *p, size_t n);
char *na_strdup(const char *s);
char *na_strndup(const char *s, size_t n);

typedef struct {
    char *p;
    size_t len, cap;
} Buf;
void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_put(Buf *b, const void *data, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, char c);
void buf_printf(Buf *b, const char *fmt, ...);
char *buf_take(Buf *b); /* NUL-terminated owned string; resets b */

typedef struct {
    char **v;
    size_t n, cap;
} StrList;
void sl_init(StrList *l);
void sl_free(StrList *l);
void sl_push(StrList *l, const char *s);
bool sl_has(const StrList *l, const char *s);
void sl_sort(StrList *l);

/* Case-insensitive (path-oriented) string set with O(1) membership. */
typedef struct {
    char **slots;
    size_t cap, n;
} StrSet;
void ss_init(StrSet *s);
void ss_free(StrSet *s);
bool ss_add(StrSet *s, const char *str); /* true if newly inserted */
bool ss_has(const StrSet *s, const char *str);

/* UTF-8 */
bool utf8_valid(const char *p, size_t n);
size_t utf8_cp_count(const char *p, size_t n);
size_t utf8_seq_len(unsigned char lead);

/* BLAKE2b, 16-byte digest (same as hashlib.blake2b(digest_size=16)). */
void blake2b16(const void *data, size_t n, uint8_t out[16]);
void hex_encode(const uint8_t *in, size_t n, char *out); /* out: 2n+1 bytes */
bool hex_decode(const char *in, uint8_t *out, size_t n);

/* Paths: '/' separated, lexically normalized, Windows case-insensitive. */
char *path_normalize(const char *path);
bool path_inside(const char *root, const char *path); /* strictly inside */
bool path_inside_or_equal(const char *root, const char *path);
bool path_equal(const char *a, const char *b);
bool path_has_suffix_ci(const char *path, const char *suffix);

#endif
