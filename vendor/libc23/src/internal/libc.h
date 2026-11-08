/*
 * libc.h - internal header shared by every source file of libc23.
 * NOT installed; never include from public headers.
 */
#ifndef LIBC23_INTERNAL_H
#define LIBC23_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <limits.h>
#include <errno.h>
#include <bits/mbstate.h>
#include "../../platform/pal.h"

#define LIBC_HIDDEN      __attribute__((visibility("hidden")))
#define LIBC_NOINLINE    __attribute__((noinline))
#define LIBC_COLD        __attribute__((cold))
#define LIBC_UNUSED      __attribute__((unused))
#define LIBC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LIBC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LIBC_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------------------------------------------------------------- */
/* Locks (built on the PAL futex, no OS dependency)                        */
/* ---------------------------------------------------------------------- */

typedef struct { volatile int32_t s; } libc_lock_t; /* 0 free, 1 held, 2 contended */
#define LIBC_LOCK_INIT { 0 }

static inline int libc_trylock(libc_lock_t *l) {
    int32_t e = 0;
    return __atomic_compare_exchange_n(&l->s, &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static inline void libc_lock(libc_lock_t *l) {
    int32_t e = 0;
    if (LIBC_LIKELY(__atomic_compare_exchange_n(&l->s, &e, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)))
        return;
    while (__atomic_exchange_n(&l->s, 2, __ATOMIC_ACQUIRE) != 0)
        pal_futex_wait(&l->s, 2, -1);
}
static inline void libc_unlock(libc_lock_t *l) {
    if (__atomic_exchange_n(&l->s, 0, __ATOMIC_RELEASE) == 2)
        pal_futex_wake(&l->s, 1);
}

/* Recursive lock (used per FILE). */
typedef struct { libc_lock_t l; volatile uintptr_t owner; int count; } libc_rlock_t;
#define LIBC_RLOCK_INIT { LIBC_LOCK_INIT, 0, 0 }
static inline void libc_rlock(libc_rlock_t *r) {
    uintptr_t me = (uintptr_t)pal_thread_self();
    if (__atomic_load_n(&r->owner, __ATOMIC_RELAXED) == me) { r->count++; return; }
    libc_lock(&r->l);
    __atomic_store_n(&r->owner, me, __ATOMIC_RELAXED);
    r->count = 1;
}
static inline void libc_runlock(libc_rlock_t *r) {
    if (--r->count == 0) {
        __atomic_store_n(&r->owner, 0, __ATOMIC_RELAXED);
        libc_unlock(&r->l);
    }
}

/* ---------------------------------------------------------------------- */
/* Per-thread libc state                                                   */
/* ---------------------------------------------------------------------- */

/* Indices into libc_tls.mbs[] : one hidden conversion state per function
 * that takes a `mbstate_t *` which may be NULL. */
enum {
    LIBC_MBS_MBRLEN, LIBC_MBS_MBRTOWC, LIBC_MBS_WCRTOMB, LIBC_MBS_MBSRTOWCS,
    LIBC_MBS_WCSRTOMBS, LIBC_MBS_MBRTOC8, LIBC_MBS_C8RTOMB, LIBC_MBS_MBRTOC16,
    LIBC_MBS_C16RTOMB, LIBC_MBS_MBRTOC32, LIBC_MBS_C32RTOMB, LIBC_MBS_COUNT
};

struct tm; /* <time.h> */

struct libc_tls {
    int        err;                       /* errno                           */
    int        _pad;
    char      *strtok_next;               /* strtok                          */
    wchar_t   *wcstok_next;               /* (wcstok has explicit state; unused) */
    mbstate_t  mbs[LIBC_MBS_COUNT];
    char       strerror_buf[96];          /* strerror for unknown numbers    */
    char       asctime_buf[32];           /* asctime / ctime                 */
    unsigned char tm_buf[64];             /* storage for gmtime/localtime result
                                             (sizeof(struct tm) <= 64 asserted in time.c) */
    void      *self;                      /* thread control block (threads module) */
    void     **tss;                       /* thread specific storage values  */
    void      *module_slot[4];            /* spare slots for modules         */
};

extern _Thread_local struct libc_tls __libc_tls LIBC_HIDDEN;
#define libc_tls() (&__libc_tls)

/* errno helper: sets errno and returns -1 (the classic error return). */
static inline int libc_seterr(int e) { errno = e; return -1; }
/* Translate a negative PAL result into errno; returns -1. */
static inline int libc_pal_fail(int pal_rc) { errno = -pal_rc; return -1; }

/* ---------------------------------------------------------------------- */
/* Cross-module internal interfaces                                        */
/* ---------------------------------------------------------------------- */

/* stdio (src/stdio) */
void __libc_stdio_flush_all(void) LIBC_HIDDEN;   /* fflush(NULL), used at exit */
void __libc_stdio_close_all(void) LIBC_HIDDEN;   /* flush + close, remove tmpfiles */

/* stdlib (src/stdlib) */
void __libc_run_atexit(void) LIBC_HIDDEN;        /* run atexit handlers LIFO   */

/* threads (src/threads): run TSS destructors of the calling thread.
 * Safe to call again after cleanup; backends call before TLS is destroyed. */
void __libc_tss_run_dtors(void) LIBC_HIDDEN;

/* signal (src/signal) */
void __libc_signal_init(void) LIBC_HIDDEN;

/* locale (src/locale): called once at startup (sets the "C" locale).
 * Character encoding rules (shared by ctype, wchar, uchar, stdlib mb*):
 *   locale "C" / "POSIX": single-byte; every byte 0x00..0xFF maps to the
 *                         wide character with the same value (Latin-1 identity).
 *   locale "C.UTF-8"    : multibyte UTF-8, MB_CUR_MAX == 4.
 * __libc_locale_utf8 is 1 iff the current LC_CTYPE is UTF-8, else 0. */
void __libc_locale_init(void) LIBC_HIDDEN;
extern int __libc_locale_utf8 LIBC_HIDDEN;

/* Unicode helpers shared by wchar/uchar/ctype/stdio (src/unicode). */
/* UTF-8 decode one scalar from s (n bytes available, n >= 1).
 * Returns bytes consumed (1..4), (size_t)-1 on invalid sequence, (size_t)-2
 * if the sequence is incomplete. *cp receives the scalar value. */
size_t __libc_utf8_decode(const unsigned char *s, size_t n, uint32_t *cp) LIBC_HIDDEN;
/* UTF-8 encode scalar cp (<= 0x10FFFF, not a surrogate) into out[4]; returns length, 0 if invalid. */
size_t __libc_utf8_encode(uint32_t cp, unsigned char *out) LIBC_HIDDEN;

/* Memory allocator internals (src/stdlib/malloc.c) */
void *__libc_malloc_internal(size_t n) LIBC_HIDDEN;

#endif /* LIBC23_INTERNAL_H */
