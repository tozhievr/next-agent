#include "json.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 256

static J *j_new(JKind k) {
    J *j = na_alloc(sizeof *j);
    j->k = k;
    return j;
}

J *j_null(void) { return j_new(J_NULL); }

J *j_bool(bool v) {
    J *j = j_new(J_BOOL);
    j->b = v;
    return j;
}

J *j_int(int64_t v) {
    J *j = j_new(J_INT);
    j->i = v;
    return j;
}

J *j_strn(const char *s, size_t n) {
    J *j = j_new(J_STR);
    j->s = na_strndup(s, n);
    j->slen = n;
    return j;
}

J *j_str(const char *s) { return j_strn(s, strlen(s)); }
J *j_arr(void) { return j_new(J_ARR); }
J *j_obj(void) { return j_new(J_OBJ); }

static void j_grow(J *j) {
    if (j->n < j->cap) return;
    j->cap = j->cap ? j->cap * 2 : 4;
    j->items = na_realloc(j->items, j->cap * sizeof *j->items);
    if (j->k == J_OBJ) j->keys = na_realloc(j->keys, j->cap * sizeof *j->keys);
}

J *j_push(J *arr, J *v) {
    j_grow(arr);
    arr->items[arr->n++] = v;
    return arr;
}

J *j_set(J *obj, const char *key, J *v) {
    for (size_t i = 0; i < obj->n; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            json_free(obj->items[i]);
            obj->items[i] = v;
            return obj;
        }
    }
    j_grow(obj);
    obj->keys[obj->n] = na_strdup(key);
    obj->items[obj->n++] = v;
    return obj;
}

const J *j_get(const J *obj, const char *key) {
    if (!obj || obj->k != J_OBJ) return NULL;
    for (size_t i = 0; i < obj->n; i++)
        if (strcmp(obj->keys[i], key) == 0) return obj->items[i];
    return NULL;
}

bool j_is_int(const J *j) { return j && j->k == J_INT; }
bool j_is_str(const J *j) { return j && j->k == J_STR; }

void json_free(J *j) {
    if (!j) return;
    for (size_t i = 0; i < j->n; i++) {
        json_free(j->items[i]);
        if (j->k == J_OBJ) free(j->keys[i]);
    }
    free(j->items);
    free(j->keys);
    free(j->s);
    free(j);
}

/* ---- parser ---- */
typedef struct {
    const char *p, *end;
    int depth;
    bool failed;
} Parser;

static J *parse_value(Parser *ps);

static void perr(Parser *ps, const char *msg) {
    if (!ps->failed) na_seterr("json: %s", msg);
    ps->failed = true;
}

static void skip_ws(Parser *ps) {
    while (ps->p < ps->end &&
           (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
        ps->p++;
}

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = v * 16 + d;
    }
    return v;
}

static void put_utf8(Buf *b, uint32_t cp) {
    if (cp < 0x80) {
        buf_putc(b, (char)cp);
    } else if (cp < 0x800) {
        buf_putc(b, (char)(0xC0 | (cp >> 6)));
        buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_putc(b, (char)(0xE0 | (cp >> 12)));
        buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        buf_putc(b, (char)(0xF0 | (cp >> 18)));
        buf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_putc(b, (char)(0x80 | (cp & 0x3F)));
    }
}

/* Parses a string body after the opening quote; returns false on error. */
static bool parse_string_raw(Parser *ps, Buf *out) {
    while (true) {
        if (ps->p >= ps->end) {
            perr(ps, "unterminated string");
            return false;
        }
        unsigned char c = (unsigned char)*ps->p++;
        if (c == '"') return true;
        if (c < 0x20) {
            perr(ps, "control character in string");
            return false;
        }
        if (c != '\\') {
            buf_putc(out, (char)c);
            continue;
        }
        if (ps->p >= ps->end) {
            perr(ps, "bad escape");
            return false;
        }
        char e = *ps->p++;
        switch (e) {
        case '"': buf_putc(out, '"'); break;
        case '\\': buf_putc(out, '\\'); break;
        case '/': buf_putc(out, '/'); break;
        case 'b': buf_putc(out, '\b'); break;
        case 'f': buf_putc(out, '\f'); break;
        case 'n': buf_putc(out, '\n'); break;
        case 'r': buf_putc(out, '\r'); break;
        case 't': buf_putc(out, '\t'); break;
        case 'u': {
            if (ps->end - ps->p < 4) {
                perr(ps, "bad unicode escape");
                return false;
            }
            int u = hex4(ps->p);
            if (u < 0) {
                perr(ps, "bad unicode escape");
                return false;
            }
            ps->p += 4;
            uint32_t cp = (uint32_t)u;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    int lo = hex4(ps->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t)(lo - 0xDC00);
                        ps->p += 6;
                    } else {
                        perr(ps, "lone surrogate");
                        return false;
                    }
                } else {
                    perr(ps, "lone surrogate");
                    return false;
                }
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                perr(ps, "lone surrogate");
                return false;
            }
            put_utf8(out, cp);
            break;
        }
        default:
            perr(ps, "bad escape");
            return false;
        }
    }
}

static J *parse_string(Parser *ps) {
    Buf b;
    buf_init(&b);
    if (!parse_string_raw(ps, &b)) {
        buf_free(&b);
        return NULL;
    }
    if (!utf8_valid(b.p ? b.p : "", b.len)) {
        buf_free(&b);
        perr(ps, "invalid UTF-8 in string");
        return NULL;
    }
    J *j = j_new(J_STR);
    j->slen = b.len;
    j->s = buf_take(&b);
    return j;
}

static J *parse_number(Parser *ps) {
    const char *s = ps->p;
    const char *q = s;
    bool is_float = false;
    if (q < ps->end && *q == '-') q++;
    if (q >= ps->end || *q < '0' || *q > '9') {
        perr(ps, "bad number");
        return NULL;
    }
    if (*q == '0') {
        q++;
        if (q < ps->end && *q >= '0' && *q <= '9') {
            perr(ps, "leading zero");
            return NULL;
        }
    } else {
        while (q < ps->end && *q >= '0' && *q <= '9') q++;
    }
    if (q < ps->end && *q == '.') {
        is_float = true;
        q++;
        if (q >= ps->end || *q < '0' || *q > '9') {
            perr(ps, "bad number");
            return NULL;
        }
        while (q < ps->end && *q >= '0' && *q <= '9') q++;
    }
    if (q < ps->end && (*q == 'e' || *q == 'E')) {
        is_float = true;
        q++;
        if (q < ps->end && (*q == '+' || *q == '-')) q++;
        if (q >= ps->end || *q < '0' || *q > '9') {
            perr(ps, "bad number");
            return NULL;
        }
        while (q < ps->end && *q >= '0' && *q <= '9') q++;
    }
    char tmp[64];
    size_t n = (size_t)(q - s);
    if (n >= sizeof tmp) {
        perr(ps, "number too long");
        return NULL;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    ps->p = q;
    if (!is_float) {
        /* detect int64 overflow, fall back to double */
        bool neg = tmp[0] == '-';
        const char *d = tmp + (neg ? 1 : 0);
        uint64_t acc = 0;
        bool ovf = false;
        for (; *d; d++) {
            uint64_t digit = (uint64_t)(*d - '0');
            if (acc > (UINT64_MAX - digit) / 10) { ovf = true; break; }
            acc = acc * 10 + digit;
        }
        if (!ovf && (neg ? acc <= (uint64_t)INT64_MAX + 1 : acc <= (uint64_t)INT64_MAX)) {
            return j_int(neg ? (int64_t)(0 - acc) : (int64_t)acc);
        }
    }
    J *j = j_new(J_DBL);
    j->d = strtod(tmp, NULL);
    return j;
}

static bool match_lit(Parser *ps, const char *lit) {
    size_t n = strlen(lit);
    if ((size_t)(ps->end - ps->p) >= n && memcmp(ps->p, lit, n) == 0) {
        ps->p += n;
        return true;
    }
    return false;
}

static J *parse_value(Parser *ps) {
    skip_ws(ps);
    if (ps->p >= ps->end) {
        perr(ps, "unexpected end");
        return NULL;
    }
    if (++ps->depth > MAX_DEPTH) {
        perr(ps, "nesting too deep");
        return NULL;
    }
    J *result = NULL;
    char c = *ps->p;
    if (c == '{') {
        ps->p++;
        result = j_obj();
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
        } else {
            while (true) {
                skip_ws(ps);
                if (ps->p >= ps->end || *ps->p != '"') {
                    perr(ps, "expected object key");
                    goto fail;
                }
                ps->p++;
                Buf kb;
                buf_init(&kb);
                if (!parse_string_raw(ps, &kb) || !utf8_valid(kb.p ? kb.p : "", kb.len)) {
                    perr(ps, "bad object key");
                    buf_free(&kb);
                    goto fail;
                }
                char *key = buf_take(&kb);
                skip_ws(ps);
                if (ps->p >= ps->end || *ps->p != ':') {
                    free(key);
                    perr(ps, "expected ':'");
                    goto fail;
                }
                ps->p++;
                J *v = parse_value(ps);
                if (!v) {
                    free(key);
                    goto fail;
                }
                j_set(result, key, v);
                free(key);
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == '}') {
                    ps->p++;
                    break;
                }
                perr(ps, "expected ',' or '}'");
                goto fail;
            }
        }
    } else if (c == '[') {
        ps->p++;
        result = j_arr();
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
        } else {
            while (true) {
                J *v = parse_value(ps);
                if (!v) goto fail;
                j_push(result, v);
                skip_ws(ps);
                if (ps->p < ps->end && *ps->p == ',') {
                    ps->p++;
                    continue;
                }
                if (ps->p < ps->end && *ps->p == ']') {
                    ps->p++;
                    break;
                }
                perr(ps, "expected ',' or ']'");
                goto fail;
            }
        }
    } else if (c == '"') {
        ps->p++;
        result = parse_string(ps);
        if (!result) goto fail;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        result = parse_number(ps);
        if (!result) goto fail;
    } else if (match_lit(ps, "true")) {
        result = j_bool(true);
    } else if (match_lit(ps, "false")) {
        result = j_bool(false);
    } else if (match_lit(ps, "null")) {
        result = j_null();
    } else {
        perr(ps, "unexpected character");
        goto fail;
    }
    ps->depth--;
    return result;
fail:
    json_free(result);
    ps->depth--;
    return NULL;
}

J *json_parse(const char *text, size_t len) {
    Parser ps = {text, text + len, 0, false};
    /* tolerate a UTF-8 BOM */
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        ps.p += 3;
    J *j = parse_value(&ps);
    if (!j) return NULL;
    skip_ws(&ps);
    if (ps.p != ps.end) {
        json_free(j);
        na_seterr("json: trailing data");
        return NULL;
    }
    return j;
}

/* ---- writer ---- */
void json_write_string(Buf *out, const char *s, size_t n) {
    buf_putc(out, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': buf_puts(out, "\\\""); break;
        case '\\': buf_puts(out, "\\\\"); break;
        case '\n': buf_puts(out, "\\n"); break;
        case '\r': buf_puts(out, "\\r"); break;
        case '\t': buf_puts(out, "\\t"); break;
        case '\b': buf_puts(out, "\\b"); break;
        case '\f': buf_puts(out, "\\f"); break;
        default:
            if (c < 0x20 || c == 0x7F) buf_printf(out, "\\u%04x", c);
            else buf_putc(out, (char)c);
        }
    }
    buf_putc(out, '"');
}

void json_write(const J *j, Buf *out) {
    switch (j->k) {
    case J_NULL: buf_puts(out, "null"); break;
    case J_BOOL: buf_puts(out, j->b ? "true" : "false"); break;
    case J_INT: buf_printf(out, "%lld", (long long)j->i); break;
    case J_DBL:
        if (isfinite(j->d)) buf_printf(out, "%.17g", j->d);
        else buf_puts(out, "null");
        break;
    case J_STR: json_write_string(out, j->s, j->slen); break;
    case J_ARR:
        buf_putc(out, '[');
        for (size_t i = 0; i < j->n; i++) {
            if (i) buf_putc(out, ',');
            json_write(j->items[i], out);
        }
        buf_putc(out, ']');
        break;
    case J_OBJ:
        buf_putc(out, '{');
        for (size_t i = 0; i < j->n; i++) {
            if (i) buf_putc(out, ',');
            json_write_string(out, j->keys[i], strlen(j->keys[i]));
            buf_putc(out, ':');
            json_write(j->items[i], out);
        }
        buf_putc(out, '}');
        break;
    }
}

char *json_dump(const J *j) {
    Buf b;
    buf_init(&b);
    json_write(j, &b);
    return buf_take(&b);
}
