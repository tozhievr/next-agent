#include <threads.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include "platform/pal.h"

static int64_t remaining(const struct timespec *ts) {
    struct pal_timespec now;
    if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L || pal_clock_gettime(PAL_CLOCK_REALTIME, &now)) return -2;
    if (ts->tv_sec < now.sec || (ts->tv_sec == now.sec && ts->tv_nsec <= now.nsec)) return 0;
    uint64_t seconds = (uint64_t)ts->tv_sec - (uint64_t)now.sec;
    if (seconds > (uint64_t)(INT64_MAX / 1000000000LL - 1)) return INT64_MAX;
    return (int64_t)seconds * 1000000000LL + ts->tv_nsec - now.nsec;
}
void call_once(once_flag *f, void (*fn)(void)) {
    int expected = 0;
    if (__atomic_compare_exchange_n(&f->__state, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
        fn(); __atomic_store_n(&f->__state, 2, __ATOMIC_RELEASE); pal_futex_wake(&f->__state, INT32_MAX);
    } else while (__atomic_load_n(&f->__state, __ATOMIC_ACQUIRE) != 2) pal_futex_wait(&f->__state, 1, -1);
}
int mtx_init(mtx_t *m, int type) {
    if (type & ~(mtx_recursive | mtx_timed)) return thrd_error;
    m->__state = 0; m->__type = type; m->__owner = 0; m->__count = 0; m->__pad = 0; return thrd_success;
}
void mtx_destroy(mtx_t *m) { (void)m; }
static int lock(mtx_t *m, const struct timespec *ts, int try) {
    unsigned long long self = (unsigned long long)pal_thread_self();
    if ((m->__type & mtx_recursive) && __atomic_load_n(&m->__owner, __ATOMIC_RELAXED) == self) {
        if (m->__count == INT_MAX) return thrd_error;
        ++m->__count; return thrd_success;
    }
    int expected = 0;
    if (!__atomic_compare_exchange_n(&m->__state, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        if (try) return thrd_busy;
        for (;;) {
            int64_t ns = ts ? remaining(ts) : -1;
            if (ns == -2) return thrd_error;
            if (!ns) return thrd_timedout;
            if (__atomic_exchange_n(&m->__state, 2, __ATOMIC_ACQUIRE) == 0) break;
            pal_futex_wait(&m->__state, 2, ns);
        }
    }
    if (m->__type & mtx_recursive) { m->__count = 1; __atomic_store_n(&m->__owner, self, __ATOMIC_RELAXED); }
    return thrd_success;
}
int mtx_lock(mtx_t *m) { return lock(m, 0, 0); }
int mtx_trylock(mtx_t *m) { return lock(m, 0, 1); }
int mtx_timedlock(mtx_t *restrict m, const struct timespec *restrict ts) { return lock(m, ts, 0); }
int mtx_unlock(mtx_t *m) {
    if (m->__type & mtx_recursive) {
        if (__atomic_load_n(&m->__owner, __ATOMIC_RELAXED) != (unsigned long long)pal_thread_self()) return thrd_error;
        if (--m->__count) return thrd_success;
        __atomic_store_n(&m->__owner, 0, __ATOMIC_RELAXED);
    }
    if (__atomic_exchange_n(&m->__state, 0, __ATOMIC_RELEASE) == 2) pal_futex_wake(&m->__state, 1);
    return thrd_success;
}
int cnd_init(cnd_t *c) { c->__seq = 0; c->__pad = 0; return thrd_success; }
void cnd_destroy(cnd_t *c) { (void)c; }
int cnd_signal(cnd_t *c) { __atomic_add_fetch(&c->__seq, 1, __ATOMIC_RELEASE); pal_futex_wake(&c->__seq, 1); return thrd_success; }
int cnd_broadcast(cnd_t *c) { __atomic_add_fetch(&c->__seq, 1, __ATOMIC_RELEASE); pal_futex_wake(&c->__seq, INT32_MAX); return thrd_success; }
static int wait_cond(cnd_t *c, mtx_t *m, const struct timespec *ts) {
    int seq = __atomic_load_n(&c->__seq, __ATOMIC_ACQUIRE);
    int64_t ns = ts ? remaining(ts) : -1;
    if (ns == -2) return thrd_error;
    if (mtx_unlock(m) != thrd_success) return thrd_error;
    int r;
    for (;;) {
        r = ns == 0 ? -ETIMEDOUT : pal_futex_wait(&c->__seq, seq, ns);
        if (r != -ETIMEDOUT || !ts) break;
        /* PAL waits may cap their relative timeout, and realtime may move. */
        if (__atomic_load_n(&c->__seq, __ATOMIC_ACQUIRE) != seq) { r = 0; break; }
        ns = remaining(ts);
        if (ns <= 0) { if (ns == -2) r = -EINVAL; break; }
    }
    if (mtx_lock(m) != thrd_success) return thrd_error;
    if (r == -ETIMEDOUT) return thrd_timedout;
    return r ? thrd_error : thrd_success;
}
int cnd_wait(cnd_t *c, mtx_t *m) { return wait_cond(c, m, 0); }
int cnd_timedwait(cnd_t *restrict c, mtx_t *restrict m, const struct timespec *restrict ts) { return wait_cond(c, m, ts); }
