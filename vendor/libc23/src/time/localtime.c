#include "time_impl.h"

/* Interned zone abbreviations: tm_zone pointers stay valid forever. */
#define MAX_ABBR 32
static char abbr_tab[MAX_ABBR][16];
static long abbr_offset[MAX_ABBR];
static int abbr_n;
static libc_lock_t abbr_lock = LIBC_LOCK_INIT;

static const char *intern(const char *s, long offset) {
    const char *r;
    libc_lock(&abbr_lock);
    int i;
    for (i = 0; i < abbr_n; i++) {
        if (abbr_offset[i] != offset) continue;
        int k = 0;
        while (abbr_tab[i][k] && abbr_tab[i][k] == s[k]) k++;
        if (abbr_tab[i][k] == s[k]) break;
    }
    if (i == abbr_n) {
        if (abbr_n >= MAX_ABBR) {
            libc_unlock(&abbr_lock);
            return NULL; /* unknown zone is safer than a mismatched abbreviation/offset */
        }
        int k = 0;
        for (; k < 15 && s[k]; k++) abbr_tab[i][k] = s[k];
        abbr_tab[i][k] = 0;
        abbr_offset[i] = offset;
        abbr_n++;
    }
    r = abbr_tab[i];
    libc_unlock(&abbr_lock);
    return r;
}

static int pointer_repr_matches(const void *field, const char *candidate) {
    /* Read the possibly indeterminate extension member as object bytes. Only
     * accept representations emitted by this module; never load/dereference it. */
    return __builtin_memcmp(field, &candidate, sizeof candidate) == 0;
}

int __libc_tm_zone_info(const struct tm *tm, const char **zone, long *offset) {
    if (tm->tm_isdst < 0) return 0;
    const char *utc = __libc_utc_zone;
    if (pointer_repr_matches(&tm->tm_zone, utc)) {
        *zone = utc;
        *offset = 0;
        return 1;
    }
    int found = 0;
    libc_lock(&abbr_lock);
    for (int i = 0; i < abbr_n; i++) {
        const char *known = abbr_tab[i];
        if (pointer_repr_matches(&tm->tm_zone, known)) {
            *zone = known;
            *offset = abbr_offset[i];
            found = 1;
            break;
        }
    }
    libc_unlock(&abbr_lock);
    return found;
}

int __libc_localtime(long long t, struct tm *tm) {
    struct pal_tzinfo tz;
    if (pal_tz_lookup(t, &tz) < 0) {
        tz.utc_offset = 0; tz.is_dst = 0;
        tz.abbr[0] = 'U'; tz.abbr[1] = 'T'; tz.abbr[2] = 'C'; tz.abbr[3] = 0;
    }
    long long lt;
    if (__builtin_add_overflow(t, (long long)tz.utc_offset, &lt)) { errno = EOVERFLOW; return -1; }
    if (__libc_secs_to_tm(lt, tm)) return -1;
    tm->tm_isdst = tz.is_dst > 0 ? 1 : tz.is_dst == 0 ? 0 : -1;
    tm->tm_gmtoff = tz.utc_offset;
    tm->tm_zone = intern(tz.abbr, tz.utc_offset);
    return 0;
}

struct tm *localtime_r(const time_t *restrict t, struct tm *restrict r) {
    return __libc_localtime(*t, r) ? NULL : r;
}

struct tm *localtime(const time_t *t) {
    return localtime_r(t, (struct tm *)libc_tls()->tm_buf);
}

static int off_at(long long u, struct pal_tzinfo *tz) {
    if (pal_tz_lookup(u, tz) < 0) { tz->utc_offset = 0; tz->is_dst = 0; }
    return tz->utc_offset;
}

time_t mktime(struct tm *tm) {
    int hint = tm->tm_isdst;
    long long L = __libc_tm_to_secs(tm);
    struct pal_tzinfo t1, t2, tv;
    long long ca[2]; int n = 0;
    ca[n++] = off_at(L - 86400, &t1);
    int o2 = off_at(L + 86400, &t2);
    if (o2 != ca[0]) ca[n++] = o2;

    int valid_off[2], valid_dst[2], nv = 0;
    for (int i = 0; i < n; i++) {
        long long u = L - ca[i];
        if (off_at(u, &tv) == ca[i]) { valid_off[nv] = (int)ca[i]; valid_dst[nv] = tv.is_dst > 0; nv++; }
    }
    long long u;
    if (nv == 0) u = L - ca[0];               /* gap: use the offset in force before it */
    else if (nv == 1) u = L - valid_off[0];
    else {
        int pick = 0;
        if (hint == 0 && valid_dst[0] && !valid_dst[1]) pick = 1;
        else if (hint > 0 && !valid_dst[0] && valid_dst[1]) pick = 1;
        u = L - valid_off[pick];
    }
    if (__libc_localtime(u, tm)) return (time_t)-1;
    return u;
}
