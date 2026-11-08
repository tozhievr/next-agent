/* stdlib.h multibyte functions: mblen mbtowc wctomb mbstowcs wcstombs.
 * Both supported encodings are stateless, so no hidden state is needed; an
 * incomplete character within n bytes is an error (C23 7.24.7). */
#include <wchar.h>
#include "wchar_impl.h"

int mblen(const char *s, size_t n) {
    mbstate_t st = { { 0, 0 } };
    uint32_t cp;
    if (!s) return 0;
    size_t r = __libc_mbrtocp(&cp, s, n, &st);
    if (r == (size_t)-2) { errno = EILSEQ; return -1; }
    if (r == (size_t)-1) return -1;
    return (int)r;
}

int mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n) {
    mbstate_t st = { { 0, 0 } };
    uint32_t cp;
    if (!s) return 0;
    size_t r = __libc_mbrtocp(&cp, s, n, &st);
    if (r == (size_t)-2) { errno = EILSEQ; return -1; }
    if (r == (size_t)-1) return -1;
    if (pwc) *pwc = (wchar_t)cp;
    return (int)r;
}

int wctomb(char *s, wchar_t wc) {
    char tmp[4];
    if (!s) return 0;
    size_t r = __libc_cptomb(tmp, (uint32_t)wc);
    if (r == (size_t)-1) return -1;
    for (size_t i = 0; i < r; i++) s[i] = tmp[i];
    return (int)r;
}

size_t mbstowcs(wchar_t *restrict pwcs, const char *restrict s, size_t n) {
    mbstate_t st = { { 0, 0 } };
    const char *p = s;
    if (!pwcs) return mbsrtowcs(NULL, &p, 0, &st);
    return mbsrtowcs(pwcs, &p, n, &st);
}

size_t wcstombs(char *restrict s, const wchar_t *restrict pwcs, size_t n) {
    mbstate_t st = { { 0, 0 } };
    const wchar_t *p = pwcs;
    if (!s) return wcsrtombs(NULL, &p, 0, &st);
    return wcsrtombs(s, &p, n, &st);
}
