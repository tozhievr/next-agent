/* internal helpers of the time module */
#ifndef LIBC23_TIME_IMPL_H
#define LIBC23_TIME_IMPL_H
#include <time.h>
#include "libc.h"

/* UTC broken-down time; returns 0, or -1 with errno = EOVERFLOW when tm_year does not fit int */
int __libc_secs_to_tm(long long t, struct tm *tm) LIBC_HIDDEN;
/* seconds since epoch for the (normalised or not) fields, interpreted as UTC. never overflows int64 */
long long __libc_tm_to_secs(const struct tm *tm) LIBC_HIDDEN;
/* local broken-down time using the PAL zone; 0 or -1 (errno set) */
int __libc_localtime(long long t, struct tm *tm) LIBC_HIDDEN;
/* Resolve zone metadata only when tm_zone's object representation matches a
 * pointer issued by a libc time conversion. Does not load an untrusted pointer. */
int __libc_tm_zone_info(const struct tm *tm, const char **zone, long *offset) LIBC_HIDDEN;
extern const char __libc_utc_zone[4] LIBC_HIDDEN;
/* Expands one conversion (modifier 0/'E'/'O' + spec char) as ASCII into out[96];
 * returns length or -1 if unknown. */
int __libc_strftime_conv(char *out, int mod, int c, const struct tm *tm) LIBC_HIDDEN;
#endif
