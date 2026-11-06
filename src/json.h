/* Minimal JSON DOM: parser and compact writer (UTF-8, ordered objects). */
#ifndef NA_JSON_H
#define NA_JSON_H
#include "util.h"

typedef enum { J_NULL, J_BOOL, J_INT, J_DBL, J_STR, J_ARR, J_OBJ } JKind;

typedef struct J J;
struct J {
    JKind k;
    bool b;
    int64_t i;
    double d;
    char *s; /* J_STR: decoded UTF-8, NUL-terminated */
    size_t slen;
    J **items; /* J_ARR elements / J_OBJ values */
    char **keys; /* J_OBJ keys */
    size_t n, cap;
};

/* Parse a complete document; returns NULL and sets na_errmsg on failure. */
J *json_parse(const char *text, size_t len);
void json_free(J *j);

J *j_null(void);
J *j_bool(bool v);
J *j_int(int64_t v);
J *j_str(const char *s);
J *j_strn(const char *s, size_t n);
J *j_arr(void);
J *j_obj(void);
J *j_push(J *arr, J *v);              /* returns arr */
J *j_set(J *obj, const char *key, J *v); /* returns obj; replaces duplicates */

const J *j_get(const J *obj, const char *key); /* NULL if absent / not an object */
bool j_is_int(const J *j);
bool j_is_str(const J *j);

void json_write(const J *j, Buf *out);
char *json_dump(const J *j); /* owned string */
void json_write_string(Buf *out, const char *s, size_t n);

#endif
