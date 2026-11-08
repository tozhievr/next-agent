#include <threads.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fenv.h>
#include "platform/pal.h"

struct __thrd { pal_thread_t handle; thrd_start_t fn; void *arg; int result; int refs; fenv_t initial_fenv; };
static _Thread_local struct __thrd *current;
static _Thread_local struct __thrd external_thread;
/* The main thread uses the same TLS-backed identity as any foreign thread. */
void __libc_threads_note_main(void) { current = &external_thread; }
void __libc_tss_run_dtors(void);
static void release(struct __thrd *t) { if (__atomic_sub_fetch(&t->refs, 1, __ATOMIC_ACQ_REL) == 0) free(t); }
static void run(void *arg) {
    struct __thrd *t = arg; current = t;
    fesetenv(&t->initial_fenv);
    int result = t->fn(t->arg);
    __libc_tss_run_dtors(); __atomic_store_n(&t->result, result, __ATOMIC_RELEASE); release(t);
}
int thrd_create(thrd_t *out, thrd_start_t fn, void *arg) {
    fenv_t initial_fenv;
    if (fegetenv(&initial_fenv)) return thrd_error;
    struct __thrd *t = malloc(sizeof *t);
    if (!t) return thrd_nomem;
    t->fn = fn; t->arg = arg; t->result = 0; t->refs = 2; t->initial_fenv = initial_fenv;
    int r = pal_thread_create(run, t, 0, &t->handle);
    if (r) { free(t); return r == -ENOMEM ? thrd_nomem : thrd_error; }
    *out = t; return thrd_success;
}
thrd_t thrd_current(void) { return current ? current : &external_thread; }
int thrd_equal(thrd_t a, thrd_t b) { return a == b; }
int thrd_detach(thrd_t t) { if (pal_thread_detach(t->handle)) return thrd_error; release(t); return thrd_success; }
int thrd_join(thrd_t t, int *res) {
    if (pal_thread_join(t->handle)) return thrd_error;
    int result = __atomic_load_n(&t->result, __ATOMIC_ACQUIRE);
    if (res) *res = result;
    release(t); return thrd_success;
}
[[noreturn]] void thrd_exit(int result) {
    __libc_tss_run_dtors();
    if (current && current != &external_thread) { __atomic_store_n(&current->result, result, __ATOMIC_RELEASE); release(current); }
    pal_thread_exit();
}
int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    if (duration->tv_sec < 0 || duration->tv_nsec < 0 || duration->tv_nsec >= 1000000000L) return -2;
    struct pal_timespec d = { duration->tv_sec, (int32_t)duration->tv_nsec }, before, after;
    int have_clock = remaining && !pal_clock_gettime(PAL_CLOCK_MONOTONIC, &before);
    int r = pal_sleep(&d);
    if (!r) return 0;
    if (r != -EINTR) return -2;
    if (remaining) {
        *remaining = *duration;
        if (have_clock && !pal_clock_gettime(PAL_CLOCK_MONOTONIC, &after)) {
            int64_t s = after.sec - before.sec; long ns = after.nsec - before.nsec;
            if (ns < 0) { --s; ns += 1000000000L; }
            if (s > duration->tv_sec || (s == duration->tv_sec && ns >= duration->tv_nsec)) { remaining->tv_sec = 0; remaining->tv_nsec = 0; }
            else { remaining->tv_sec -= s; remaining->tv_nsec -= ns; if (remaining->tv_nsec < 0) { --remaining->tv_sec; remaining->tv_nsec += 1000000000L; } }
        }
    }
    return -1;
}
void thrd_yield(void) { pal_thread_yield(); }
