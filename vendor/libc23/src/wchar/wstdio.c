/* Wide-oriented character I/O. Stream locking and raw byte access are
 * supplied by the stdio core; the two supported encodings are stateless. */
#include <wchar.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "../stdio/file.h"
#include "stdio_impl.h"

size_t __libc_utf8_decode(const unsigned char *, size_t, uint32_t *);
size_t __libc_utf8_encode(uint32_t, unsigned char *);

wint_t __libc_getwc_unlocked(FILE *f) {
    if (__libc_fwide_unlocked(f, 1) < 0) return WEOF;
    int c = __libc_wide_getbyte_unlocked(f);
    if (c == EOF) return WEOF;
    if (!f->wide_utf8) return (wint_t)(unsigned char)c;

    unsigned char bytes[4] = {(unsigned char)c, 0, 0, 0};
    uint32_t cp;
    for (size_t n = 1; n <= sizeof bytes; ++n) {
        size_t result = __libc_utf8_decode(bytes, n, &cp);
        if (result != (size_t)-1 && result != (size_t)-2) return (wint_t)cp;
        if (result == (size_t)-1 || n == sizeof bytes) break;
        int next = __libc_wide_getbyte_unlocked(f);
        if (next == EOF) {
            if (__libc_ferror_unlocked(f)) return WEOF;
            break;
        }
        bytes[n] = (unsigned char)next;
    }
    errno = EILSEQ;
    __libc_wide_error_unlocked(f);
    return WEOF;
}

wint_t __libc_putwc_unlocked(wchar_t wc, FILE *f) {
    if (__libc_fwide_unlocked(f, 1) < 0) return WEOF;
    unsigned char bytes[4];
    size_t n;
    if (f->wide_utf8) n = __libc_utf8_encode((uint32_t)wc, bytes);
    else if ((uint32_t)wc <= 0xFF) { bytes[0] = (unsigned char)wc; n = 1; }
    else n = 0;
    if (!n) { errno = EILSEQ; __libc_wide_error_unlocked(f); return WEOF; }
    for (size_t i = 0; i < n; ++i)
        if (__libc_wide_putbyte_unlocked((unsigned char)bytes[i], f) == EOF) return WEOF;
    return (wint_t)wc;
}

wint_t __libc_ungetwc_unlocked(wint_t wc, FILE *f) {
    if (wc == WEOF || __libc_fwide_unlocked(f, 1) < 0) return WEOF;
    unsigned char bytes[4];
    size_t n;
    if (f->wide_utf8) n = __libc_utf8_encode((uint32_t)wc, bytes);
    else if ((uint32_t)wc <= 0xFF) { bytes[0] = (unsigned char)wc; n = 1; }
    else n = 0;
    if (!n) return WEOF;
    return __libc_wide_unget_bytes_unlocked((const unsigned char *)bytes, n, f) ? wc : WEOF;
}

int fwide(FILE *f, int mode) {
    __libc_flockfile(f); int r = __libc_fwide_unlocked(f, mode); __libc_funlockfile(f); return r;
}
wint_t fgetwc(FILE *f) {
    __libc_flockfile(f); wint_t r = __libc_getwc_unlocked(f); __libc_funlockfile(f); return r;
}
wint_t getwc(FILE *f) { return fgetwc(f); }
wint_t getwchar(void) { return fgetwc(stdin); }
wint_t fputwc(wchar_t c, FILE *f) {
    __libc_flockfile(f); wint_t r = __libc_putwc_unlocked(c, f); __libc_funlockfile(f); return r;
}
wint_t putwc(wchar_t c, FILE *f) { return fputwc(c, f); }
wint_t putwchar(wchar_t c) { return fputwc(c, stdout); }
wint_t ungetwc(wint_t c, FILE *f) {
    __libc_flockfile(f); wint_t r = __libc_ungetwc_unlocked(c, f); __libc_funlockfile(f); return r;
}
wchar_t *fgetws(wchar_t *restrict s, int n, FILE *restrict f) {
    if (n <= 0) return NULL;
    __libc_flockfile(f);
    if (__libc_fwide_unlocked(f, 1) < 0) { __libc_funlockfile(f); return NULL; }
    unsigned old_error = f->flags & F_ERR;
    f->flags &= ~F_ERR;
    int i = 0;
    int encountered_eof = 0;
    while (i < n - 1) {
        wint_t c = __libc_getwc_unlocked(f);
        if (c == WEOF) { encountered_eof = 1; break; }
        s[i++] = (wchar_t)c;
        if (c == L'\n') break;
    }
    int error = encountered_eof && __libc_ferror_unlocked(f);
    f->flags |= old_error;
    if (!error && (i || n == 1)) s[i] = 0;
    __libc_funlockfile(f);
    return !error && (i || n == 1) ? s : NULL;
}
int fputws(const wchar_t *restrict s, FILE *restrict f) {
    __libc_flockfile(f);
    if (__libc_fwide_unlocked(f, 1) < 0) { __libc_funlockfile(f); return EOF; }
    while (*s) if (__libc_putwc_unlocked(*s++, f) == WEOF) { __libc_funlockfile(f); return EOF; }
    __libc_funlockfile(f);
    return 0;
}
