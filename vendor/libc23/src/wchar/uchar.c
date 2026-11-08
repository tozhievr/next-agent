/* <uchar.h>: mbrtoc8 c8rtomb mbrtoc16 c16rtomb mbrtoc32 c32rtomb */
#include <uchar.h>
#include "wchar_impl.h"

size_t mbrtoc32(char32_t *restrict pc32, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBRTOC32];
    uint32_t cp;
    if (!s) { s = ""; n = 1; pc32 = NULL; }
    size_t r = __libc_mbrtocp(&cp, s, n, ps);
    if (r <= (size_t)-3 && pc32) *pc32 = cp;
    return r;
}

size_t c32rtomb(char *restrict s, char32_t c32, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_C32RTOMB];
    if (!s) { __libc_mbs_reset(ps); return 1; }
    size_t r = __libc_cptomb(s, c32);
    if (r != (size_t)-1) __libc_mbs_reset(ps);
    return r;
}

size_t mbrtoc16(char16_t *restrict pc16, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBRTOC16];
    if (!s) { s = ""; n = 1; pc16 = NULL; }
    if (ps->__s[1] & MBS_LO_PEND) {              /* second half of a surrogate pair */
        if (pc16) *pc16 = (char16_t)ps->__s[0];
        __libc_mbs_reset(ps);
        return (size_t)-3;
    }
    uint32_t cp;
    size_t r = __libc_mbrtocp(&cp, s, n, ps);
    if (r > (size_t)-3) return r;
    if (cp >= 0x10000) {
        uint32_t v = cp - 0x10000;
        if (pc16) *pc16 = (char16_t)(0xD800 + (v >> 10));
        ps->__s[0] = 0xDC00 + (v & 0x3FF);
        ps->__s[1] = MBS_LO_PEND;
    } else if (pc16) {
        *pc16 = (char16_t)cp;
    }
    return r;
}

size_t c16rtomb(char *restrict s, char16_t c16, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_C16RTOMB];
    char internal[4];
    if (!s) { s = internal; c16 = 0; }
    if (ps->__s[1] & MBS_HI_PEND) {
        uint32_t hi = ps->__s[0];
        __libc_mbs_reset(ps);
        if (c16 < 0xDC00 || c16 > 0xDFFF) { errno = EILSEQ; return (size_t)-1; }
        return __libc_cptomb(s, 0x10000 + ((hi - 0xD800) << 10) + (c16 - 0xDC00));
    }
    if (c16 >= 0xD800 && c16 <= 0xDBFF) {
        ps->__s[0] = c16;
        ps->__s[1] = MBS_HI_PEND;
        return 0;
    }
    if (c16 >= 0xDC00 && c16 <= 0xDFFF) { errno = EILSEQ; return (size_t)-1; }
    size_t r = __libc_cptomb(s, c16);
    if (r != (size_t)-1) __libc_mbs_reset(ps);
    return r;
}

size_t mbrtoc8(char8_t *restrict pc8, const char *restrict s, size_t n, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_MBRTOC8];
    if (!s) { s = ""; n = 1; pc8 = NULL; }
    if (ps->__s[1] & MBS_C8OUT) {                /* more code units of a converted character */
        unsigned cnt = (ps->__s[1] >> 8) & 7;
        char8_t b = (char8_t)(ps->__s[0] & 0xFF);
        ps->__s[0] >>= 8;
        if (--cnt == 0) __libc_mbs_reset(ps);
        else ps->__s[1] = MBS_C8OUT | (cnt << 8);
        if (pc8) *pc8 = b;
        return (size_t)-3;
    }
    uint32_t cp;
    size_t r = __libc_mbrtocp(&cp, s, n, ps);
    if (r > (size_t)-3) return r;
    unsigned char u[4];
    size_t l = __libc_utf8_encode(cp, u);
    if (pc8) *pc8 = u[0];
    if (l > 1) {
        ps->__s[0] = (uint32_t)u[1] | (uint32_t)(l > 2 ? u[2] : 0) << 8 | (uint32_t)(l > 3 ? u[3] : 0) << 16;
        ps->__s[1] = MBS_C8OUT | ((uint32_t)(l - 1) << 8);
    }
    return r;
}

size_t c8rtomb(char *restrict s, char8_t c8, mbstate_t *restrict ps) {
    if (!ps) ps = &libc_tls()->mbs[LIBC_MBS_C8RTOMB];
    char internal[4];
    if (!s) { s = internal; c8 = 0; }
    unsigned char tmp[4];
    unsigned cnt = MBS_CNT(ps);
    if (cnt > 3) cnt = 0;
    for (unsigned i = 0; i < cnt; i++) tmp[i] = (unsigned char)(ps->__s[0] >> (8 * i));
    tmp[cnt++] = c8;
    uint32_t cp;
    size_t r = __libc_utf8_decode(tmp, cnt, &cp);
    if (r == (size_t)-2) {
        uint32_t packed = 0;
        for (unsigned i = 0; i < cnt; i++) packed |= (uint32_t)tmp[i] << (8 * i);
        ps->__s[0] = packed;
        ps->__s[1] = cnt;
        return 0;
    }
    __libc_mbs_reset(ps);
    if (r == (size_t)-1) { errno = EILSEQ; return (size_t)-1; }
    return __libc_cptomb(s, cp);
}
