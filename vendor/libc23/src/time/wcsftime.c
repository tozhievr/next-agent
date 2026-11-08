/* wcsftime: same engine as strftime; the prototype lives in <wchar.h>
 * (declared locally so this file builds independently). */
#include "time_impl.h"

size_t wcsftime(wchar_t *restrict s, size_t max, const wchar_t *restrict fmt,
                const struct tm *restrict tm);

size_t wcsftime(wchar_t *restrict s, size_t max, const wchar_t *restrict fmt,
                const struct tm *restrict tm) {
    size_t n = 0;
    char buf[96];
    if (max == 0) return 0;
    for (; *fmt; fmt++) {
        if (*fmt != L'%') {
            if (n + 1 >= max) return 0;
            s[n++] = *fmt;
            continue;
        }
        const wchar_t *start = fmt;
        int mod = 0;
        fmt++;
        if (*fmt == L'E' || *fmt == L'O') mod = (int)*fmt++;
        int k = (*fmt > 0 && *fmt < 128) ? __libc_strftime_conv(buf, mod, (int)*fmt, tm) : -1;
        if (k < 0) {
            size_t len = (size_t)(fmt - start) + (*fmt ? 1 : 0);
            if (n + len >= max) return 0;
            for (size_t i = 0; i < len; i++) s[n++] = start[i];
            if (!*fmt) break;
            continue;
        }
        if (n + (size_t)k >= max) return 0;
        for (int i = 0; i < k; i++) s[n++] = (wchar_t)(unsigned char)buf[i];
    }
    s[n] = 0;
    return n;
}
