/* Shared by <time.h> and <threads.h>. */
#ifndef _LIBC23_BITS_TIMESPEC_H
#define _LIBC23_BITS_TIMESPEC_H
typedef long long time_t;
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};
#endif
