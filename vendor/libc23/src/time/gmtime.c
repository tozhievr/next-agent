#include "time_impl.h"

_Static_assert(sizeof(struct tm) <= 64, "struct tm must fit libc_tls.tm_buf");

const char __libc_utc_zone[4] LIBC_HIDDEN = "UTC";

static long long floordiv(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* days since 1970-01-01 of a civil date (proleptic Gregorian) */
static long long days_from_civil(long long y, int m, int d) {
    y -= m <= 2;
    long long era = floordiv(y, 400);
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int __libc_secs_to_tm(long long t, struct tm *tm) {
    long long days = floordiv(t, 86400);
    long long rem = t - days * 86400;
    long long z = days + 719468;
    long long era = floordiv(z, 146097);
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) y++;
    if (y - 1900 > 2147483647LL || y - 1900 < -2147483647LL - 1) {
        errno = EOVERFLOW;
        return -1;
    }
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min = (int)(rem % 3600 / 60);
    tm->tm_sec = (int)(rem % 60);
    long long wd = (days + 4) % 7;
    tm->tm_wday = (int)(wd < 0 ? wd + 7 : wd);
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    tm->tm_gmtoff = 0;
    tm->tm_zone = __libc_utc_zone;
    return 0;
}

long long __libc_tm_to_secs(const struct tm *tm) {
    long long y = (long long)tm->tm_year + 1900;
    long long mon = tm->tm_mon;
    long long yq = floordiv(mon, 12);
    y += yq;
    mon -= yq * 12;
    long long days = days_from_civil(y, (int)mon + 1, 1) + ((long long)tm->tm_mday - 1);
    return days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
}

struct tm *gmtime_r(const time_t *restrict t, struct tm *restrict r) {
    return __libc_secs_to_tm(*t, r) ? NULL : r;
}

struct tm *gmtime(const time_t *t) {
    struct tm *r = (struct tm *)libc_tls()->tm_buf;
    return gmtime_r(t, r);
}

time_t timegm(struct tm *tm) {
    long long t = __libc_tm_to_secs(tm);
    if (__libc_secs_to_tm(t, tm)) return (time_t)-1;
    return t;
}
