#include "time_impl.h"

static const char wd[7][4] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char mn[12][4] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

static char *fmt(const struct tm *tm, char *b) {
    long long y = (long long)tm->tm_year + 1900;
    if (y < 0 || y > 9999) { errno = EOVERFLOW; return NULL; }
    if (tm->tm_wday < 0 || tm->tm_wday > 6 || tm->tm_mon < 0 || tm->tm_mon > 11 ||
        tm->tm_mday < 0 || tm->tm_mday > 99 || tm->tm_hour < 0 || tm->tm_hour > 99 ||
        tm->tm_min < 0 || tm->tm_min > 99 || tm->tm_sec < 0 || tm->tm_sec > 99) {
        errno = EINVAL; return NULL;
    }
    int i = 0;
    for (int k = 0; k < 3; k++) b[i++] = wd[tm->tm_wday][k];
    b[i++] = ' ';
    for (int k = 0; k < 3; k++) b[i++] = mn[tm->tm_mon][k];
    b[i++] = ' ';
    b[i++] = tm->tm_mday >= 10 ? (char)('0' + tm->tm_mday / 10) : ' ';
    b[i++] = (char)('0' + tm->tm_mday % 10);
    b[i++] = ' ';
    int v[3] = { tm->tm_hour, tm->tm_min, tm->tm_sec };
    for (int k = 0; k < 3; k++) {
        b[i++] = (char)('0' + v[k] / 10); b[i++] = (char)('0' + v[k] % 10);
        b[i++] = k < 2 ? ':' : ' ';
    }
    b[i++] = (char)('0' + y / 1000); b[i++] = (char)('0' + y / 100 % 10);
    b[i++] = (char)('0' + y / 10 % 10); b[i++] = (char)('0' + y % 10);
    b[i++] = '\n'; b[i] = 0;
    return b;
}

char *asctime(const struct tm *tm) { return fmt(tm, libc_tls()->asctime_buf); }

char *ctime(const time_t *t) {
    struct tm tmp;
    if (__libc_localtime(*t, &tmp)) return NULL;
    return fmt(&tmp, libc_tls()->asctime_buf);
}
