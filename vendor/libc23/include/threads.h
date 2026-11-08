/* <threads.h> - C23 7.28 (libc23) */
#ifndef _LIBC23_THREADS_H
#define _LIBC23_THREADS_H

#include <bits/timespec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __thrd *thrd_t;
typedef int (*thrd_start_t)(void *);
typedef unsigned int tss_t;
typedef void (*tss_dtor_t)(void *);

typedef struct mtx_t {
    int                __state;   /* futex word: 0 free, 1 locked, 2 contended */
    int                __type;
    unsigned long long __owner;   /* owner thread id (recursive mutexes)      */
    int                __count;   /* recursion depth                          */
    int                __pad;
} mtx_t;

typedef struct cnd_t {
    int __seq;                    /* futex word, bumped by signal/broadcast   */
    int __pad;
} cnd_t;

typedef struct once_flag {
    int __state;                  /* 0 fresh, 1 running, 2 done               */
} once_flag;

#define ONCE_FLAG_INIT { 0 }
#define TSS_DTOR_ITERATIONS 4

enum {
    mtx_plain     = 0,
    mtx_recursive = 1,
    mtx_timed     = 2
};

enum {
    thrd_success  = 0,
    thrd_busy     = 1,
    thrd_error    = 2,
    thrd_nomem    = 3,
    thrd_timedout = 4
};

void call_once(once_flag *__flag, void (*__func)(void));

int  cnd_broadcast(cnd_t *__cond);
void cnd_destroy(cnd_t *__cond);
int  cnd_init(cnd_t *__cond);
int  cnd_signal(cnd_t *__cond);
int  cnd_timedwait(cnd_t *restrict __cond, mtx_t *restrict __mtx,
                   const struct timespec *restrict __ts);
int  cnd_wait(cnd_t *__cond, mtx_t *__mtx);

void mtx_destroy(mtx_t *__mtx);
int  mtx_init(mtx_t *__mtx, int __type);
int  mtx_lock(mtx_t *__mtx);
int  mtx_timedlock(mtx_t *restrict __mtx, const struct timespec *restrict __ts);
int  mtx_trylock(mtx_t *__mtx);
int  mtx_unlock(mtx_t *__mtx);

int   thrd_create(thrd_t *__thr, thrd_start_t __func, void *__arg);
thrd_t thrd_current(void);
int   thrd_detach(thrd_t __thr);
int   thrd_equal(thrd_t __a, thrd_t __b);
[[noreturn]] void thrd_exit(int __res);
int   thrd_join(thrd_t __thr, int *__res);
int   thrd_sleep(const struct timespec *__duration, struct timespec *__remaining);
void  thrd_yield(void);

int   tss_create(tss_t *__key, tss_dtor_t __dtor);
void  tss_delete(tss_t __key);
void *tss_get(tss_t __key);
int   tss_set(tss_t __key, void *__val);

#ifdef __cplusplus
}
#endif
#endif
