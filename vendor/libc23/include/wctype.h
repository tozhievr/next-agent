/* <wctype.h> - C23 7.32 (libc23).
 * wctype_t / wctrans_t are `unsigned long` handles (0 = invalid).
 * Locale "C": only ASCII is classified/mapped.  Locale "C.UTF-8": full
 * Unicode (see tools/gen_unicode.py for the exact definitions). */
#ifndef _LIBC23_WCTYPE_H
#define _LIBC23_WCTYPE_H

#define __need_wint_t
#include <stddef.h>
#ifndef WEOF
#define WEOF (0xFFFFFFFFu)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long wctype_t;
typedef unsigned long wctrans_t;

int iswalnum(wint_t __wc);
int iswalpha(wint_t __wc);
int iswblank(wint_t __wc);
int iswcntrl(wint_t __wc);
int iswdigit(wint_t __wc);
int iswgraph(wint_t __wc);
int iswlower(wint_t __wc);
int iswprint(wint_t __wc);
int iswpunct(wint_t __wc);
int iswspace(wint_t __wc);
int iswupper(wint_t __wc);
int iswxdigit(wint_t __wc);
int iswctype(wint_t __wc, wctype_t __desc);
wctype_t wctype(const char *__property);
wint_t towlower(wint_t __wc);
wint_t towupper(wint_t __wc);
wint_t towctrans(wint_t __wc, wctrans_t __desc);
wctrans_t wctrans(const char *__property);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_WCTYPE_H */
