/* C23 strfrom* conversions, sharing the exact floating formatter core. */
#include "libc.h"
#include <stdlib.h>
#include "../stdio/fmt_core.h"

struct strfrom_out {
    char *dst;
    size_t cap;
    size_t count;
};

static void strfrom_puts(void *ctx, const char *s, size_t n) {
    struct strfrom_out *o = (struct strfrom_out *)ctx;
    size_t room = o->cap > o->count ? o->cap - o->count : 0;
    size_t copy = n < room ? n : room;
    if (copy) __builtin_memcpy(o->dst + o->count, s, copy);
    if (n > SIZE_MAX - o->count) o->count = SIZE_MAX;
    else o->count += n;
}

static void strfrom_fill(void *ctx, char c, size_t n) {
    struct strfrom_out *o = (struct strfrom_out *)ctx;
    size_t room = o->cap > o->count ? o->cap - o->count : 0;
    size_t copy = n < room ? n : room;
    if (copy) __builtin_memset(o->dst + o->count, c, copy);
    if (n > SIZE_MAX - o->count) o->count = SIZE_MAX;
    else o->count += n;
}

static int strfrom_format(char *s, size_t n, const char *format,
                          const struct fmt_fp *fp) {
    if (!format || format[0] != '%') { errno = EINVAL; return -1; }
    const char *p = format + 1;
    int precision = -1;
    if (*p == '.') {
        ++p;
        precision = 0;
        while (*p >= '0' && *p <= '9') {
            int digit = *p++ - '0';
            if (precision > (INT_MAX - digit) / 10) { errno = EOVERFLOW; return -1; }
            precision = precision * 10 + digit;
        }
    }
    int conv = (unsigned char)*p++;
    if (!conv || *p || !(conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                         conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G')) {
        errno = EINVAL; return -1;
    }

    struct strfrom_out out = { s, n ? n - 1 : 0, 0 };
    fmt_emitter emitter = { &out, strfrom_puts, strfrom_fill };
    __libc_fmt_float(&emitter, 0, 0, precision, conv, fp);
    if (n) s[out.count < n - 1 ? out.count : n - 1] = '\0';
    if (out.count > INT_MAX) { errno = EOVERFLOW; return -1; }
    return (int)out.count;
}

int strfromd(char *restrict s, size_t n, const char *restrict format, double value) {
    struct fmt_fp fp;
    __libc_fmt_decode_double(value, &fp);
    return strfrom_format(s, n, format, &fp);
}

int strfromf(char *restrict s, size_t n, const char *restrict format, float value) {
    struct fmt_fp fp;
    __libc_fmt_decode_double((double)value, &fp);
    return strfrom_format(s, n, format, &fp);
}

int strfroml(char *restrict s, size_t n, const char *restrict format, long double value) {
    struct fmt_fp fp;
    __libc_fmt_decode_ldouble(value, &fp);
    return strfrom_format(s, n, format, &fp);
}
