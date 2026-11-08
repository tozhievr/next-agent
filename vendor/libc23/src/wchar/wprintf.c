/* Wide formatted output. Numeric directives use the narrow formatter; its
 * output is ASCII in the supported locales. */
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include "stdio_impl.h"

struct out { FILE *f; wchar_t *s; size_t cap, n; int failed; };
static void emit(struct out *o, wchar_t c) {
    if (o->failed) return;
    if (o->n == INT_MAX) { errno = EOVERFLOW; o->failed = 1; return; }
    if (o->f) { if (__libc_putwc_unlocked(c, o->f) == WEOF) { o->failed = 1; return; } }
    else if (o->n < o->cap) o->s[o->n] = c;
    o->n++;
}
static void pad(struct out *o, size_t n) { while (n-- && !o->failed) emit(o, L' '); }
static int number(const wchar_t **p) {
    int n = 0;
    while (**p >= L'0' && **p <= L'9') {
        int d = *(*p)++ - L'0';
        n = n > (INT_MAX - d) / 10 ? INT_MAX : n * 10 + d;
    }
    return n;
}
static void decimal(char **p, unsigned n) {
    char a[16]; int i = 0;
    do { a[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) *(*p)++ = a[--i];
}
enum { NONE, HH, H, L, LL, J, Z, T, CAPL, W8, W16, W32, W64, WF8, WF16, WF32, WF64 };
#define CONSUME_SIGNED(ap, type) ((void)_Generic((type)0, signed char: va_arg(ap, int), short: va_arg(ap, int), int: va_arg(ap, int), long: va_arg(ap, long), long long: va_arg(ap, long long)))
#define CONSUME_UNSIGNED(ap, type) ((void)_Generic((type)0, unsigned char: va_arg(ap, int), unsigned short: va_arg(ap, int), unsigned int: va_arg(ap, unsigned int), unsigned long: va_arg(ap, unsigned long), unsigned long long: va_arg(ap, unsigned long long)))
#define CONSUME_FIXED(ap, len, prefix, macro) do { switch (len) { \
    case W8: macro(ap, prefix##8_t); break; case W16: macro(ap, prefix##16_t); break; \
    case W32: macro(ap, prefix##32_t); break; case W64: macro(ap, prefix##64_t); break; \
    case WF8: macro(ap, prefix##_fast8_t); break; case WF16: macro(ap, prefix##_fast16_t); break; \
    case WF32: macro(ap, prefix##_fast32_t); break; case WF64: macro(ap, prefix##_fast64_t); break; \
    } } while (0)
static int format(struct out *o, const wchar_t *fmt, va_list incoming) {
    va_list ap; va_copy(ap, incoming);
    while (*fmt && !o->failed) {
        if (*fmt != L'%') { emit(o, *fmt++); continue; }
        fmt++;
        if (*fmt == L'%') { emit(o, *fmt++); continue; }
        char flags[8]; int nf = 0, left = 0;
        while (*fmt == L'-' || *fmt == L'+' || *fmt == L' ' || *fmt == L'#' || *fmt == L'0' || *fmt == L'\'') {
            if (*fmt == L'-') left = 1;
            if (nf < 7) flags[nf++] = (char)*fmt;
            fmt++;
        }
        int width = 0, precision = -1;
        if (*fmt == L'*') { width = va_arg(ap, int); fmt++; if (width < 0) { left = 1; width = width == INT_MIN ? INT_MAX : -width; } }
        else width = number(&fmt);
        if (*fmt == L'.') {
            fmt++;
            if (*fmt == L'*') { precision = va_arg(ap, int); fmt++; if (precision < 0) precision = -1; }
            else precision = number(&fmt);
        }
        int len = NONE;
        if (*fmt == L'h') { fmt++; len = *fmt == L'h' ? (fmt++, HH) : H; }
        else if (*fmt == L'l') { fmt++; len = *fmt == L'l' ? (fmt++, LL) : L; }
        else if (*fmt == L'j') { fmt++; len = J; }
        else if (*fmt == L'z') { fmt++; len = Z; }
        else if (*fmt == L't') { fmt++; len = T; }
        else if (*fmt == L'L') { fmt++; len = CAPL; }
        else if (*fmt == L'w') {
            fmt++; int fast = 0;
            if (*fmt == L'f') { fast = 1; fmt++; }
            int bits = number(&fmt);
            len = bits == 8 ? W8 : bits == 16 ? W16 : bits == 32 ? W32 : bits == 64 ? W64 : NONE;
            if (fast && len != NONE) len += WF8 - W8;
        }
        wchar_t conv = *fmt;
        if (!conv) { o->failed = 1; break; }
        fmt++;
        if (conv == L'n') {
            switch (len) {
            case W8: *va_arg(ap, int8_t *) = (int8_t)o->n; break;
            case W16: *va_arg(ap, int16_t *) = (int16_t)o->n; break;
            case W32: *va_arg(ap, int32_t *) = (int32_t)o->n; break;
            case W64: *va_arg(ap, int64_t *) = (int64_t)o->n; break;
            case WF8: *va_arg(ap, int_fast8_t *) = (int_fast8_t)o->n; break;
            case WF16: *va_arg(ap, int_fast16_t *) = (int_fast16_t)o->n; break;
            case WF32: *va_arg(ap, int_fast32_t *) = (int_fast32_t)o->n; break;
            case WF64: *va_arg(ap, int_fast64_t *) = (int_fast64_t)o->n; break;
            case HH: *va_arg(ap, signed char *) = (signed char)o->n; break;
            case H: *va_arg(ap, short *) = (short)o->n; break;
            case L: *va_arg(ap, long *) = (long)o->n; break;
            case LL: *va_arg(ap, long long *) = (long long)o->n; break;
            case J: *va_arg(ap, intmax_t *) = (intmax_t)o->n; break;
            case Z: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)o->n; break;
            case T: *va_arg(ap, ptrdiff_t *) = (ptrdiff_t)o->n; break;
            default: *va_arg(ap, int *) = (int)o->n; break;
            }
        } else if (conv == L'c') {
            wint_t c = len == L ? va_arg(ap, wint_t) : btowc((unsigned char)va_arg(ap, int));
            if (c == WEOF) { errno = EILSEQ; o->failed = 1; break; }
            if (!left && width > 1) pad(o, (size_t)width - 1);
            emit(o, (wchar_t)c);
            if (left && width > 1) pad(o, (size_t)width - 1);
        } else if (conv == L's') {
            const wchar_t *ws = NULL; const char *bs = NULL;
            if (len == L) ws = va_arg(ap, const wchar_t *); else bs = va_arg(ap, const char *);
            if (!ws && !bs) { ws = L"(null)"; len = L; }
            size_t count = 0; mbstate_t st = {{0, 0}};
            if (len == L) { while ((precision < 0 || count < (size_t)precision) && ws[count]) count++; }
            else {
                const char *p = bs;
                while ((precision < 0 || count < (size_t)precision) && *p) {
                    wchar_t wc; size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &st);
                    if (n == (size_t)-1 || n == (size_t)-2) { errno = EILSEQ; o->failed = 1; break; }
                    p += n; count++;
                }
            }
            if (o->failed) break;
            if (!left && (size_t)width > count) pad(o, (size_t)width - count);
            if (len == L) for (size_t i = 0; i < count; i++) emit(o, ws[i]);
            else {
                const char *p = bs; st = (mbstate_t){{0, 0}};
                for (size_t i = 0; i < count; i++) { wchar_t wc; size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &st); emit(o, wc); p += n; }
            }
            if (left && (size_t)width > count) pad(o, (size_t)width - count);
        } else {
            char spec[80], *q = spec; *q++ = '%';
            if (left) *q++ = '-';
            for (int i = 0; i < nf; i++) if (flags[i] != '-') *q++ = flags[i];
            if (width) decimal(&q, (unsigned)width);
            if (precision >= 0) { *q++ = '.'; decimal(&q, (unsigned)precision); }
            if (len == HH) { *q++ = 'h'; *q++ = 'h'; }
            else if (len == H) *q++ = 'h';
            else if (len == L) *q++ = 'l';
            else if (len == LL) { *q++ = 'l'; *q++ = 'l'; }
            else if (len == J) *q++ = 'j';
            else if (len == Z) *q++ = 'z';
            else if (len == T) *q++ = 't';
            else if (len == CAPL) *q++ = 'L';
            else if (len >= W8 && len <= WF64) {
                *q++ = 'w'; int index = len - W8;
                if (index >= 4) { *q++ = 'f'; index -= 4; }
                decimal(&q, index == 0 ? 8 : index == 1 ? 16 : index == 2 ? 32 : 64);
            }
            if (conv > 127) { o->failed = 1; break; }
            *q++ = (char)conv; *q = 0;
            va_list cp; va_copy(cp, ap); int n = vsnprintf(NULL, 0, spec, cp); va_end(cp);
            if (n < 0) { o->failed = 1; break; }
            char stack[256], *buf = stack;
            if ((size_t)n >= sizeof stack) { buf = malloc((size_t)n + 1); if (!buf) { o->failed = 1; break; } }
            va_copy(cp, ap); int r = vsnprintf(buf, (size_t)n + 1, spec, cp); va_end(cp);
            if (r < 0) o->failed = 1;
            else for (int i = 0; i < r; i++) emit(o, (unsigned char)buf[i]);
            if (buf != stack) free(buf);
            if (conv == L'd' || conv == L'i') {
                if (len >= W8 && len <= WF64) CONSUME_FIXED(ap, len, int, CONSUME_SIGNED);
                else if (len == L) (void)va_arg(ap, long); else if (len == LL) (void)va_arg(ap, long long);
                else if (len == J) (void)va_arg(ap, intmax_t); else if (len == Z || len == T) (void)va_arg(ap, ptrdiff_t); else (void)va_arg(ap, int);
            } else if (conv == L'u' || conv == L'o' || conv == L'x' || conv == L'X' || conv == L'b' || conv == L'B') {
                if (len >= W8 && len <= WF64) CONSUME_FIXED(ap, len, uint, CONSUME_UNSIGNED);
                else if (len == L) (void)va_arg(ap, unsigned long); else if (len == LL) (void)va_arg(ap, unsigned long long);
                else if (len == J) (void)va_arg(ap, uintmax_t); else if (len == Z || len == T) (void)va_arg(ap, size_t); else (void)va_arg(ap, unsigned int);
            } else if (conv == L'p') (void)va_arg(ap, void *);
            else if (conv == L'a' || conv == L'A' || conv == L'e' || conv == L'E' || conv == L'f' || conv == L'F' || conv == L'g' || conv == L'G') {
                if (len == CAPL) (void)va_arg(ap, long double); else (void)va_arg(ap, double);
            } else { o->failed = 1; }
        }
    }
    va_end(ap); return o->failed ? -1 : (int)o->n;
}
int vfwprintf(FILE *restrict f, const wchar_t *restrict fmt, va_list ap) {
    struct out o = { .f = f }; __libc_flockfile(f);
    int r = __libc_fwide_unlocked(f, 1) < 0 ? -1 : format(&o, fmt, ap);
    __libc_funlockfile(f); return r;
}
int fwprintf(FILE *restrict f, const wchar_t *restrict fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfwprintf(f, fmt, ap); va_end(ap); return r; }
int vwprintf(const wchar_t *restrict fmt, va_list ap) { return vfwprintf(stdout, fmt, ap); }
int wprintf(const wchar_t *restrict fmt, ...) { va_list ap; va_start(ap, fmt); int r = vwprintf(fmt, ap); va_end(ap); return r; }
int vswprintf(wchar_t *restrict s, size_t n, const wchar_t *restrict fmt, va_list ap) {
    struct out o = { .s = s, .cap = n }; int r = format(&o, fmt, ap);
    if (r < 0 || o.n >= n) return -1;
    s[o.n] = 0; return r;
}
int swprintf(wchar_t *restrict s, size_t n, const wchar_t *restrict fmt, ...) { va_list ap; va_start(ap, fmt); int r = vswprintf(s, n, fmt, ap); va_end(ap); return r; }
