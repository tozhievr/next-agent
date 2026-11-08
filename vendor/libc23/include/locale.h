/* <locale.h> - C23 7.11 (libc23)
 *
 * Supported locales (all categories): "C" (also "POSIX"), and "C.UTF-8"
 * (also "C.utf8").  setlocale returns the canonical names "C" / "C.UTF-8".
 * LC_CTYPE selects the character encoding: in "C" bytes map 1:1 to wide
 * characters (Latin-1 identity, MB_CUR_MAX 1), in "C.UTF-8" the multibyte
 * encoding is UTF-8 (MB_CUR_MAX 4) and the isw*() / tow*() functions use full
 * Unicode data.  The decimal point is "." in every locale.
 */
#ifndef _LIBC23_LOCALE_H
#define _LIBC23_LOCALE_H

#define __need_NULL
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char *currency_symbol;
    char frac_digits;
    char p_cs_precedes;
    char n_cs_precedes;
    char p_sep_by_space;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
    char *int_curr_symbol;
    char int_frac_digits;
    char int_p_cs_precedes;
    char int_n_cs_precedes;
    char int_p_sep_by_space;
    char int_n_sep_by_space;
    char int_p_sign_posn;
    char int_n_sign_posn;
};

#define LC_CTYPE    0
#define LC_NUMERIC  1
#define LC_TIME     2
#define LC_COLLATE  3
#define LC_MONETARY 4
#define LC_ALL      6

char *setlocale(int __category, const char *__locale);
struct lconv *localeconv(void);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_LOCALE_H */
