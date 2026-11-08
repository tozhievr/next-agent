/*
 * pal.h - Platform Abstraction Layer (PAL) contract for libc23.
 *
 * The portable part of the library (src/) talks to the host ONLY through the
 * functions declared here.  A port to a new OS / bare metal / hypervisor means
 * writing platform/<name>/ that implements every function below, plus the
 * process entry point (see "Startup contract").  No other file in src/ may
 * include OS headers, issue system calls, or use OS-specific types.
 *
 * Conventions
 *  - Every function returning int returns 0 (or a non-negative value where
 *    documented) on success and a NEGATIVE libc errno value (-ENOENT, ...) on
 *    failure.  Backends translate native error codes to the numbers in
 *    <errno.h>.
 *  - All paths and strings crossing the boundary are UTF-8, NUL terminated.
 *  - All functions are thread-safe unless stated otherwise.
 *  - The backend must guarantee that C11 `_Thread_local` objects work in every
 *    thread (main thread and threads made by pal_thread_create).
 *  - Nothing here may call back into the libc except the hooks listed in
 *    "Hooks provided by the portable libc".
 *
 * Startup contract
 *  The backend owns the process entry symbol (__libc_mainCRTStartup, _start, ...).
 *  It must: set up stack/TLS, convert the command line and the environment to
 *  UTF-8 `char **` vectors (NULL terminated, storage lives until exit), do any
 *  console setup it wants, and finally call
 *        _Noreturn void __libc_start_main(int argc, char **argv, char **envp);
 *  which is provided by src/startup/start.c.  Termination of the process is
 *  done only through pal_exit()/pal_abort().
 */
#ifndef LIBC23_PAL_H
#define LIBC23_PAL_H

/* PAL names are a private source contract.  Backends and internal callers
 * include this header so their linker symbols use the reserved libc prefix;
 * applications may independently define identifiers beginning with pal_. */
#define pal_name __libc_pal_name
#define pal_path_sep __libc_pal_path_sep
#define pal_environ __libc_pal_environ
#define pal_exit __libc_pal_exit
#define pal_abort __libc_pal_abort
#define pal_system __libc_pal_system
#define pal_random __libc_pal_random
#define pal_page_size __libc_pal_page_size
#define pal_mem_map __libc_pal_mem_map
#define pal_mem_unmap __libc_pal_mem_unmap
#define pal_open __libc_pal_open
#define pal_close __libc_pal_close
#define pal_read __libc_pal_read
#define pal_write __libc_pal_write
#define pal_seek __libc_pal_seek
#define pal_sync __libc_pal_sync
#define pal_isatty __libc_pal_isatty
#define pal_std_file __libc_pal_std_file
#define pal_remove __libc_pal_remove
#define pal_rename __libc_pal_rename
#define pal_tmpdir __libc_pal_tmpdir
#define pal_clock_gettime __libc_pal_clock_gettime
#define pal_clock_getres __libc_pal_clock_getres
#define pal_sleep __libc_pal_sleep
#define pal_tz_lookup __libc_pal_tz_lookup
#define pal_thread_create __libc_pal_thread_create
#define pal_thread_join __libc_pal_thread_join
#define pal_thread_detach __libc_pal_thread_detach
#define pal_thread_exit __libc_pal_thread_exit
#define pal_thread_self __libc_pal_thread_self
#define pal_thread_yield __libc_pal_thread_yield
#define pal_futex_wait __libc_pal_futex_wait
#define pal_futex_wake __libc_pal_futex_wake
#define pal_signal_enable __libc_pal_signal_enable

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Identification                                                           */
/* ------------------------------------------------------------------------ */

/* Human readable backend name, e.g. "windows-x86_64".  Never NULL. */
extern const char pal_name[];

/* Path separator preferred when libc builds paths itself (tmpnam, ...). */
extern const char pal_path_sep; /* '/' or '\\' */

/* ------------------------------------------------------------------------ */
/* Process                                                                  */
/* ------------------------------------------------------------------------ */

/* Terminate the process immediately with the given status (no atexit, no
 * stdio flush: that is done by libc before it calls this). */
_Noreturn void pal_exit(int status);

/* Abnormal termination (used after abort() could not use a SIGABRT handler). */
_Noreturn void pal_abort(void);

/* Environment as handed over by the startup code. Valid after startup. */
extern char **pal_environ;

/* Run `command` through the host command interpreter.
 *   command == NULL : return 1 if an interpreter exists, 0 otherwise (as value
 *                     of *status, function returns 0).
 *   otherwise       : *status receives the termination status of the command
 *                     (implementation defined meaning, 0 == success).
 * Returns 0 if the interpreter could be started, negative errno otherwise. */
int pal_system(const char *command, int *status);

/* Fill buf with n cryptographically strong random bytes. */
int pal_random(void *buf, size_t n);

/* ------------------------------------------------------------------------ */
/* Memory                                                                   */
/* ------------------------------------------------------------------------ */

size_t pal_page_size(void);

/* Map `size` bytes (a multiple of pal_page_size()) of zero-filled read/write
 * memory aligned to at least pal_page_size().  Returns NULL on failure. */
void *pal_mem_map(size_t size);

/* Release memory obtained from pal_mem_map (ptr/size exactly as mapped, or
 * any page aligned sub-range on platforms that allow it - libc only unmaps
 * whole mappings). */
int pal_mem_unmap(void *ptr, size_t size);

/* ------------------------------------------------------------------------ */
/* Files                                                                    */
/* ------------------------------------------------------------------------ */

typedef intptr_t pal_file_t;
#define PAL_INVALID_FILE ((pal_file_t)-1)

#define PAL_O_RDONLY 0x01 /* read access                                    */
#define PAL_O_WRONLY 0x02 /* write access                                   */
#define PAL_O_RDWR   0x03
#define PAL_O_CREAT  0x04 /* create if missing                              */
#define PAL_O_TRUNC  0x08 /* truncate to zero length                        */
#define PAL_O_EXCL   0x10 /* with CREAT: fail with -EEXIST if present       */
#define PAL_O_APPEND 0x20 /* every write goes to the end of file            */
#define PAL_O_TEMP   0x40 /* file disappears when closed / on exit          */

#define PAL_SEEK_SET 0
#define PAL_SEEK_CUR 1
#define PAL_SEEK_END 2

/* No text-mode newline translation is performed anywhere in libc23:
 * text and binary streams are identical. */

int pal_open(const char *path, int flags, pal_file_t *out);
int pal_close(pal_file_t f);
/* Reads up to n bytes.  *got == 0 && return 0 means end of file. */
int pal_read(pal_file_t f, void *buf, size_t n, size_t *got);
/* Writes up to n bytes; may write less (libc loops). */
int pal_write(pal_file_t f, const void *buf, size_t n, size_t *written);
/* Reposition; *newpos receives the resulting absolute offset. Fails with
 * -ESPIPE for non seekable streams. */
int pal_seek(pal_file_t f, int64_t off, int whence, int64_t *newpos);
/* Flush OS level buffers to storage (best effort). */
int pal_sync(pal_file_t f);
/* 1 if f refers to an interactive terminal, else 0. */
int pal_isatty(pal_file_t f);
/* Standard streams: 0 = stdin, 1 = stdout, 2 = stderr.  May return
 * PAL_INVALID_FILE if absent (reads then fail with -EBADF). */
pal_file_t pal_std_file(int n);

int pal_remove(const char *path);                         /* file or empty dir */
int pal_rename(const char *from, const char *to);         /* replaces target   */

/* Directory suitable for temporary files, WITH trailing path separator,
 * written to buf (size n).  Returns 0 or -ERANGE/-errno. */
int pal_tmpdir(char *buf, size_t n);

/* ------------------------------------------------------------------------ */
/* Time                                                                     */
/* ------------------------------------------------------------------------ */

struct pal_timespec {
    int64_t sec;
    int32_t nsec; /* 0 .. 999999999 */
};

#define PAL_CLOCK_REALTIME    0 /* wall clock, seconds since 1970-01-01 UTC   */
#define PAL_CLOCK_MONOTONIC   1 /* monotonic, arbitrary epoch                 */
#define PAL_CLOCK_PROCESS_CPU 2 /* CPU time used by the process               */
#define PAL_CLOCK_THREAD_CPU  3 /* CPU time used by the calling thread        */

int pal_clock_gettime(int clock, struct pal_timespec *ts);
int pal_clock_getres(int clock, struct pal_timespec *ts);

/* Sleep for (at least) the given duration. Returns 0, or -EINTR if woken
 * early (remaining time is not reported). */
int pal_sleep(const struct pal_timespec *duration);

/* Local time zone information for the UTC instant `utc` (seconds since epoch).
 * `abbr` is a short designation such as "UTC", "CET", "+0530". */
struct pal_tzinfo {
    int32_t utc_offset; /* seconds EAST of UTC (e.g. +3600 for CET)          */
    int32_t is_dst;     /* > 0 daylight saving in effect, 0 not, < 0 unknown */
    char    abbr[16];
};
int pal_tz_lookup(int64_t utc, struct pal_tzinfo *out);

/* ------------------------------------------------------------------------ */
/* Threads and synchronisation                                              */
/* ------------------------------------------------------------------------ */

typedef uintptr_t pal_thread_t; /* join handle, never 0 for a valid thread  */
typedef uintptr_t pal_tid_t;    /* identity of a running thread, never 0    */

/* Start a thread running fn(arg).  stack_size == 0 selects a default. */
int  pal_thread_create(void (*fn)(void *), void *arg, size_t stack_size,
                       pal_thread_t *out);
/* Wait for the thread to finish and release its resources. */
int  pal_thread_join(pal_thread_t t);
/* Release resources automatically at thread exit. */
int  pal_thread_detach(pal_thread_t t);
/* Terminate the calling thread (not the process). */
_Noreturn void pal_thread_exit(void);
/* Id of the calling thread; unique among live threads, nonzero.  It is NOT
 * the same value as the pal_thread_t join handle (libc records the tid of a
 * new thread in its own control block when the thread starts). */
pal_tid_t pal_thread_self(void);
void pal_thread_yield(void);

/* Futex-style wait/wake on a 32-bit word, the only blocking primitive used
 * by mutexes, condition variables, call_once and thread join in libc.
 *
 *   pal_futex_wait: atomically checks *addr == expected and sleeps until a
 *     wake, a timeout or a spurious wake-up.  Returns 0 (woken, spurious or
 *     value mismatch) or -ETIMEDOUT.  timeout_ns < 0 means "no timeout".
 *   pal_futex_wake: wakes up to `count` waiters (INT32_MAX == all).
 *
 * A backend without OS support may implement these with a spin/yield loop
 * (wait = bounded yield+sleep, wake = no-op), correctness only requires that
 * waiters re-check their condition after each return. */
int  pal_futex_wait(volatile int32_t *addr, int32_t expected, int64_t timeout_ns);
void pal_futex_wake(volatile int32_t *addr, int32_t count);

/* ------------------------------------------------------------------------ */
/* Signals                                                                  */
/* ------------------------------------------------------------------------ */

/* libc signal numbers (see <signal.h>): SIGINT 2, SIGILL 4, SIGABRT 6,
 * SIGFPE 8, SIGSEGV 11, SIGTERM 15.
 *
 * Ask the backend to start (on != 0) or stop (on == 0) translating the
 * corresponding host event / hardware fault into a call of
 * __libc_signal_dispatch(sig).  Returns 0, or -EINVAL if the host cannot
 * generate that signal (then raise() is the only source). */
int pal_signal_enable(int sig, int on);

/* ------------------------------------------------------------------------ */
/* Hooks provided by the portable libc (called BY the backend)              */
/* ------------------------------------------------------------------------ */

/* Deliver `sig` to the handler registered with signal().  Executes in the
 * context of the faulting/interrupted thread (or a helper thread for
 * console interrupts).  If it returns, the backend decides what to do:
 * for SIGSEGV/SIGFPE/SIGILL it must terminate via pal_abort(); for SIGINT
 * and SIGTERM execution simply continues.  When no user handler is
 * installed libc itself applies the default action (pal_exit / pal_abort)
 * and never returns. */
void __libc_signal_dispatch(int sig);

/* Run destructors for the calling thread's TSS values before its TLS is
 * destroyed.  Safe to call again after portable thread cleanup.  Backends
 * invoke this on host thread exit when they can observe it; on Windows the
 * loader invokes it under its loader lock, so destructors must avoid work
 * requiring that lock.  Process termination does not invoke this hook. */
void __libc_tss_run_dtors(void);

/* Entry into the portable runtime, see "Startup contract". */
_Noreturn void __libc_start_main(int argc, char **argv, char **envp);

#ifdef __cplusplus
}
#endif
#endif /* LIBC23_PAL_H */
