/* mbsinit mbrlen mbrtowc wcrtomb mbsrtowcs wcsrtombs btowc wctob + core */
#include <wchar.h>
#include "wchar_impl.h"

size_t __libc_mbrtocp(uint32_t *out, const char *s, size_t n, mbstate_t *ps) {
    if (n == 0) return (size_t)-2;
    if (!__libc_locale_utf8) {
        __libc_mbs_reset(ps);
        uint32_t c = (unsigned char)*s;
        *out = c;
        return c ? 1 : 0;
    }
    unsigned char tmp[4];
    unsigned cnt = MBS_CNT(ps);
    if (cnt > 3) cnt = 0;
    for (unsigned i = 0; i < cnt; i++) tmp[i] = (unsigned char)(ps->__s[0] >> (8 * i));
    size_t used = 0;
    for (;;) {
        if (used == n) {
            uint32_t packed = 0;
            for (unsigned i = 0; i < cnt; i++) packed |= (uint32_t)tmp[i] << (8 * i);
            ps->__s[0] = packed;
            ps->__s[1] = cnt;
            return (size_t)-2;
        }
        tmp[cnt++] = (unsigned char)s[used++];
        uint32_t cp;
        size_t r = __libc_utf8_decode(tmp, cnt, &cp);
        if (r == (size_t)-2) continue;
        __libc_mbs_reset(ps);
        if (r == (size_t)-1) { errno = EILSEQ; return (size_t)-1; }
        *out = cp;
        return cp ? used : 0;
    }
}

size_t __libc_cptomb(char *s, uint32_t cp) {
    if (!__libc_locale_utf8) {
        if (cp > 0xFF) { errno = EILSEQ; return (size_t)-1; }
        *s = (char)cp;
        return 1;
    }
    size_t l = __libc_utf8_encode(cp, (unsigned char *)s);
    if (!l) { errno = EILSEQ; return (size_t)-1; }
    return l;
}

int mbsinit(const mbstate_t *ps) {
    return !ps || __libc_mbs_initial(ps);
}

size_t mbrtowc(wchar_t *restrict pwc, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBRTOWC];
    uint32_t cp;
    if (!s) { s = ""; n = 1; pwc = NULL; }
    size_t r = __libc_mbrtocp(&cp, s, n, ps);
    if (r <= (size_t)-3 && pwc) *pwc = (wchar_t)cp;
    return r;
}

size_t mbrlen(const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBRLEN];
    uint32_t cp;
    if (!s) { s = ""; n = 1; }
    return __libc_mbrtocp(&cp, s, n, ps);
}

size_t wcrtomb(char *restrict s, wchar_t wc, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_WCRTOMB];
    if (!s) { __libc_mbs_reset(ps); return 1; }
    size_t r = __libc_cptomb(s, (uint32_t)wc);
    if (r != (size_t)-1) __libc_mbs_reset(ps);
    return r;
}

size_t mbsrtowcs(wchar_t *restrict dst, const char **restrict src, size_t len, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBSRTOWCS];
    const char *s = *src;
    size_t count = 0;
    for (;;) {
        if (dst && count == len) break;
        /* examine at most 4 bytes plus the terminator */
        size_t avail = 0;
        while (avail < 4 && s[avail]) avail++;
        uint32_t cp;
        size_t r = __libc_mbrtocp(&cp, s, avail + 1, ps);
        if (r == (size_t)-1) { if (dst) *src = s; return (size_t)-1; }
        if (r == (size_t)-2) { if (dst) *src = s; errno = EILSEQ; return (size_t)-1; }
        if (r == 0) {
            if (dst) { dst[count] = 0; *src = NULL; }
            __libc_mbs_reset(ps);
            return count;
        }
        if (dst) dst[count] = (wchar_t)cp;
        count++;
        s += r;
    }
    if (dst) *src = s;
    return count;
}

size_t wcsrtombs(char *restrict dst, const wchar_t **restrict src, size_t len, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_WCSRTOMBS];
    const wchar_t *w = *src;
    size_t count = 0;
    for (;;) {
        if (dst && count == len) break;
        char tmp[4];
        uint32_t c = (uint32_t)*w;
        if (c == 0) {
            if (dst) {
                if (count >= len) break;      /* no room for the terminator */
                dst[count] = 0;
                *src = NULL;
            }
            __libc_mbs_reset(ps);
            return count;
        }
        size_t l = __libc_cptomb(tmp, c);
        if (l == (size_t)-1) { if (dst) *src = w; return (size_t)-1; }
        if (dst) {
            if (len - count < l) break;
            for (size_t i = 0; i < l; i++) dst[count + i] = tmp[i];
        }
        count += l;
        w++;
    }
    if (dst) *src = w;
    return count;
}

wint_t btowc(int c) {
    if (c < 0 || c > 255) return WEOF;
    if (__libc_locale_utf8 && c >= 0x80) return WEOF;
    return (wint_t)c;
}

int wctob(wint_t c) {
    if (c < (__libc_locale_utf8 ? 0x80u : 0x100u)) return (int)c;
    return -1;
}
