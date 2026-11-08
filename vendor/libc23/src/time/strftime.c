/* strftime conversion engine (ASCII, C locale).  wcsftime reuses
 * __libc_strftime_conv from wcsftime.c. */
#include "time_impl.h"

static const char *const wday_full[7] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *const mon_full[12] = {"January","February","March","April","May","June","July",
                                          "August","September","October","November","December"};

static long long fdiv(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
static long long fmod_(long long a, long long b) { return a - fdiv(a, b) * b; }

/* append v with at least `width` digits padded with `pad` ('0', ' ' or 0 for none) */
static int put_num(char *o, long long v, int width, char pad) {
    char tmp[24]; int n = 0, len = 0;
    unsigned long long u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) {
        o[len++] = '-';
        width--;
    }
    for (int k = n; k < width; k++) o[len++] = pad;
    while (n) o[len++] = tmp[--n];
    return len;
}

static int put_str(char *o, const char *s) {
    int n = 0;
    while (s[n]) { o[n] = s[n]; n++; }
    return n;
}

static int iso_weeks(long long y) {
    /* Jan 1 weekday, Sunday == 0. A year has 53 ISO weeks when it
     * starts Thursday, or when it is a leap year starting Wednesday. */
    long long y0 = y - 1;
    long long jan1 = fmod_(y + fdiv(y0, 4) - fdiv(y0, 100) + fdiv(y0, 400), 7);
    int leap = (fmod_(y, 4) == 0) && (fmod_(y, 100) != 0 || fmod_(y, 400) == 0);
    return (jan1 == 4 || (leap && jan1 == 3)) ? 53 : 52;
}

static void iso_week(const struct tm *tm, long long *year, int *week) {
    long long y = (long long)tm->tm_year + 1900;
    int wd = tm->tm_wday == 0 ? 7 : tm->tm_wday;
    int w = (tm->tm_yday + 1 - wd + 10) / 7;
    if (w < 1) { y--; w = iso_weeks(y); }
    else if (w == 53 && iso_weeks(y) == 52) { y++; w = 1; }
    *year = y; *week = w;
}

/* expand narrow format into o (>= 96 bytes of room guaranteed per conversion) */
static int expand(char *o, const char *fmt, const struct tm *tm) {
    int n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { o[n++] = *fmt; continue; }
        int mod = 0;
        fmt++;
        if (*fmt == 'E' || *fmt == 'O') mod = *fmt++;
        int k = __libc_strftime_conv(o + n, mod, *fmt, tm);
        if (k < 0) k = 0;
        n += k;
        if (!*fmt) break;
    }
    return n;
}

int __libc_strftime_conv(char *o, int mod, int c, const struct tm *tm) {
    long long year = (long long)tm->tm_year + 1900;
    /* validate modifier combinations: invalid ones are printed literally */
    if (mod == 'E' && !(c == 'c' || c == 'C' || c == 'x' || c == 'X' || c == 'y' || c == 'Y')) goto bad;
    if (mod == 'O' && !(c == 'b' || c == 'B' || c == 'd' || c == 'e' || c == 'H' || c == 'I' || c == 'm' || c == 'M' ||
                         c == 'S' || c == 'u' || c == 'U' || c == 'V' || c == 'w' || c == 'W' || c == 'y')) goto bad;
    switch (c) {
    case 'a': {
        int k = put_str(o, (unsigned)tm->tm_wday < 7 ? wday_full[tm->tm_wday] : "?");
        return k > 3 ? 3 : k;
    }
    case 'A': return put_str(o, (unsigned)tm->tm_wday < 7 ? wday_full[tm->tm_wday] : "?");
    case 'b': case 'h': {
        int k = put_str(o, (unsigned)tm->tm_mon < 12 ? mon_full[tm->tm_mon] : "?");
        return k > 3 ? 3 : k;
    }
    case 'B': return put_str(o, (unsigned)tm->tm_mon < 12 ? mon_full[tm->tm_mon] : "?");
    case 'c': return expand(o, "%a %b %e %H:%M:%S %Y", tm);
    case 'C': return put_num(o, fdiv(year, 100), 2, '0');
    case 'd': return put_num(o, tm->tm_mday, 2, '0');
    case 'D': return expand(o, "%m/%d/%y", tm);
    case 'e': return put_num(o, tm->tm_mday, 2, ' ');
    case 'F': return expand(o, "%Y-%m-%d", tm);
    case 'g': { long long y; int w; iso_week(tm, &y, &w); return put_num(o, fmod_(y, 100), 2, '0'); }
    case 'G': { long long y; int w; iso_week(tm, &y, &w); return put_num(o, y, 1, '0'); }
    case 'H': return put_num(o, tm->tm_hour, 2, '0');
    case 'I': { int h = tm->tm_hour % 12; return put_num(o, h == 0 ? 12 : h, 2, '0'); }
    case 'j': return put_num(o, (long long)tm->tm_yday + 1, 3, '0');
    case 'm': return put_num(o, (long long)tm->tm_mon + 1, 2, '0');
    case 'M': return put_num(o, tm->tm_min, 2, '0');
    case 'n': o[0] = '\n'; return 1;
    case 'p': return put_str(o, tm->tm_hour >= 12 ? "PM" : "AM");
    case 'r': return expand(o, "%I:%M:%S %p", tm);
    case 'R': return expand(o, "%H:%M", tm);
    case 'S': return put_num(o, tm->tm_sec, 2, '0');
    case 't': o[0] = '\t'; return 1;
    case 'T': return expand(o, "%H:%M:%S", tm);
    case 'u': return put_num(o, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0');
    case 'U': return put_num(o, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0');
    case 'V': { long long y; int w; iso_week(tm, &y, &w); return put_num(o, w, 2, '0'); }
    case 'w': return put_num(o, tm->tm_wday, 1, '0');
    case 'W': return put_num(o, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0');
    case 'x': return expand(o, "%m/%d/%y", tm);
    case 'X': return expand(o, "%H:%M:%S", tm);
    case 'y': return put_num(o, fmod_(year, 100), 2, '0');
    case 'Y': return put_num(o, year, 1, '0');
    case 'z': {
        const char *zone;
        long offset;
        if (!__libc_tm_zone_info(tm, &zone, &offset)) return 0;
        long long off = offset;
        int n = 0;
        o[n++] = off < 0 ? '-' : '+';
        if (off < 0) off = -off;
        n += put_num(o + n, off / 3600 % 100, 2, '0');
        n += put_num(o + n, off % 3600 / 60, 2, '0');
        return n;
    }
    case 'Z': {
        const char *z;
        long offset;
        if (!__libc_tm_zone_info(tm, &z, &offset)) return 0;
        int n = 0;
        while (z[n] && n < 90) { o[n] = z[n]; n++; }
        return n;
    }
    case '%': o[0] = '%'; return 1;
    default: break;
    }
bad:
    return -1;
}

size_t strftime(char *restrict s, size_t max, const char *restrict fmt, const struct tm *restrict tm) {
    size_t n = 0;
    char buf[96];
    if (max == 0) return 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (n + 1 >= max) return 0;
            s[n++] = *fmt;
            continue;
        }
        const char *start = fmt;
        int mod = 0;
        fmt++;
        if (*fmt == 'E' || *fmt == 'O') mod = *fmt++;
        int k = *fmt ? __libc_strftime_conv(buf, mod, *fmt, tm) : -1;
        if (k < 0) {
            /* unknown conversion: copy the sequence literally */
            size_t len = (size_t)(fmt - start) + (*fmt ? 1 : 0);
            if (n + len >= max) return 0;
            for (size_t i = 0; i < len; i++) s[n++] = start[i];
            if (!*fmt) break;
            continue;
        }
        if (n + (size_t)k >= max) return 0;
        for (int i = 0; i < k; i++) s[n++] = buf[i];
    }
    s[n] = 0;
    return n;
}
