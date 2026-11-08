/* internal: multibyte core shared by wchar, uchar and stdlib mb* functions */
#ifndef LIBC23_WCHAR_IMPL_H
#define LIBC23_WCHAR_IMPL_H
#include "libc.h"

/* State layout (mbstate_t):
 *   __s[0]  pending bytes (little endian packed) or pending surrogate / c8 bytes
 *   __s[1]  bits 0..7  count of pending input bytes (partial multibyte / UTF-8 char)
 *           bits 8..10 count of pending UTF-8 output bytes (mbrtoc8)
 *           bit  29    pending UTF-8 output bytes valid (mbrtoc8)
 *           bit  30    high surrogate pending (c16rtomb), value in __s[0]
 *           bit  31    low surrogate pending (mbrtoc16), value in __s[0]        */
#define MBS_CNT(ps)   ((ps)->__s[1] & 0xFFu)
#define MBS_C8OUT     (1u << 29)
#define MBS_HI_PEND   (1u << 30)
#define MBS_LO_PEND   (1u << 31)

/* Convert one multibyte character of the current locale to a scalar value.
 * s != NULL.  Returns bytes consumed from s (>=1), 0 for U+0000, (size_t)-1
 * (EILSEQ) or (size_t)-2 (incomplete: all n bytes absorbed into *ps; with
 * n == 0 -2 is returned and *ps untouched). */
size_t __libc_mbrtocp(uint32_t *out, const char *s, size_t n, mbstate_t *ps) LIBC_HIDDEN;
/* Encode scalar for the current locale; returns length (1..4) or (size_t)-1/EILSEQ. */
size_t __libc_cptomb(char *s, uint32_t cp) LIBC_HIDDEN;

/* iswspace-like test used by wcsto* (ASCII only in the C locale; Unicode
 * White_Space minus the no-break spaces in UTF-8 locales). */
int __libc_wc_isspace(uint32_t c) LIBC_HIDDEN;

static inline int __libc_mbs_initial(const mbstate_t *ps) { return !ps->__s[0] && !ps->__s[1]; }
static inline void __libc_mbs_reset(mbstate_t *ps) { ps->__s[0] = ps->__s[1] = 0; }
#endif
