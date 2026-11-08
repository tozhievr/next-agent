#include "time_impl.h"

static int pal_clock_for(int base) {
    switch (base) {
    case TIME_UTC: return PAL_CLOCK_REALTIME;
    case TIME_MONOTONIC: return PAL_CLOCK_MONOTONIC;
    case TIME_ACTIVE: return PAL_CLOCK_PROCESS_CPU;
    case TIME_THREAD_ACTIVE: return PAL_CLOCK_THREAD_CPU;
    default: return -1;
    }
}

clock_t clock(void) {
    struct pal_timespec ts;
    if (pal_clock_gettime(PAL_CLOCK_PROCESS_CPU, &ts) < 0) return (clock_t)-1;
    if (ts.nsec < 0 || ts.nsec >= 1000000000) return (clock_t)-1;
    long long whole, result;
    if (__builtin_mul_overflow((long long)ts.sec, (long long)CLOCKS_PER_SEC, &whole) ||
        __builtin_add_overflow(whole, (long long)ts.nsec / (1000000000LL / CLOCKS_PER_SEC), &result))
        return (clock_t)-1;
    return (clock_t)result;
}

time_t time(time_t *t) {
    struct pal_timespec ts;
    time_t r = (pal_clock_gettime(PAL_CLOCK_REALTIME, &ts) < 0) ? (time_t)-1 : (time_t)ts.sec;
    if (t) *t = r;
    return r;
}

double difftime(time_t a, time_t b) {
    /* Subtract in the unsigned domain so nearby large times retain their
     * difference without signed overflow, then convert the magnitude once. */
    if (a >= b) return (double)((unsigned long long)a - (unsigned long long)b);
    return -(double)((unsigned long long)b - (unsigned long long)a);
}

int timespec_get(struct timespec *ts, int base) {
    int c = pal_clock_for(base);
    struct pal_timespec p;
    if (c < 0 || pal_clock_gettime(c, &p) < 0) return 0;
    ts->tv_sec = p.sec; ts->tv_nsec = p.nsec;
    return base;
}

int timespec_getres(struct timespec *ts, int base) {
    int c = pal_clock_for(base);
    if (c < 0) return 0;
    /* C23 permits ts to be null; the return value still reports support. */
    if (!ts) return base;
    struct pal_timespec p;
    if (pal_clock_getres(c, &p) < 0) return 0;
    ts->tv_sec = p.sec; ts->tv_nsec = p.nsec;
    return base;
}
