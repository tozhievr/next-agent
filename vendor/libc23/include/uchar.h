/* <uchar.h> - C23 7.30 (libc23). */
#ifndef _LIBC23_UCHAR_H
#define _LIBC23_UCHAR_H

#define __STDC_VERSION_UCHAR_H__ 202311L

#define __need_size_t
#include <stddef.h>
#include <stdint.h>
#include <bits/mbstate.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char char8_t;
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;

#ifndef __STDC_UTF_16__
#define __STDC_UTF_16__ 1
#endif
#ifndef __STDC_UTF_32__
#define __STDC_UTF_32__ 1
#endif

size_t mbrtoc8(char8_t *restrict pc8, const char *restrict s, size_t n, mbstate_t *restrict ps);
size_t c8rtomb(char *restrict s, char8_t c8, mbstate_t *restrict ps);
size_t mbrtoc16(char16_t *restrict pc16, const char *restrict s, size_t n, mbstate_t *restrict ps);
size_t c16rtomb(char *restrict s, char16_t c16, mbstate_t *restrict ps);
size_t mbrtoc32(char32_t *restrict pc32, const char *restrict s, size_t n, mbstate_t *restrict ps);
size_t c32rtomb(char *restrict s, char32_t c32, mbstate_t *restrict ps);

#ifdef __cplusplus
}
#endif
#endif
