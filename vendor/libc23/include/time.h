/* <time.h> - C23 7.29 (libc23). */
#ifndef _LIBC23_TIME_H
#define _LIBC23_TIME_H

#define __need_size_t
#define __need_NULL
#include <stddef.h>
#include <bits/timespec.h>

#define __STDC_VERSION_TIME_H__ 202311L

#ifdef __cplusplus
extern "C" {
#endif

typedef long long clock_t;

#define CLOCKS_PER_SEC ((clock_t)1000000)

#define TIME_UTC           1
#define TIME_MONOTONIC     2
#define TIME_ACTIVE        3
#define TIME_THREAD_ACTIVE 4

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

clock_t clock(void);
double  difftime(time_t __t1, time_t __t0);
time_t  mktime(struct tm *__tm);
time_t  time(time_t *__t);
time_t  timegm(struct tm *__tm);
int     timespec_get(struct timespec *__ts, int __base);
int     timespec_getres(struct timespec *__ts, int __base);

char   *asctime(const struct tm *__tm);
char   *ctime(const time_t *__t);
struct tm *gmtime(const time_t *__t);
struct tm *gmtime_r(const time_t *restrict __t, struct tm *restrict __r);
struct tm *localtime(const time_t *__t);
struct tm *localtime_r(const time_t *restrict __t, struct tm *restrict __r);
size_t  strftime(char *restrict __s, size_t __max, const char *restrict __fmt,
                 const struct tm *restrict __tm);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_TIME_H */
