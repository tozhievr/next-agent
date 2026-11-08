/* libc23 PAL backend for Linux (x86-64). Raw system calls only: no host libc. */
#include "../pal.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "platform/linux currently supports x86_64 Linux only"
#endif

/* ---------------------------------------------------------------------- */
/* System calls                                                            */
/* ---------------------------------------------------------------------- */
enum {
    SYS_read = 0, SYS_write = 1, SYS_close = 3, SYS_lseek = 8, SYS_mmap = 9, SYS_mprotect = 10, SYS_munmap = 11,
    SYS_rt_sigaction = 13, SYS_ioctl = 16, SYS_access = 21, SYS_sched_yield = 24, SYS_nanosleep = 35,
    SYS_getpid = 39, SYS_fork = 57, SYS_execve = 59, SYS_exit = 60, SYS_wait4 = 61, SYS_kill = 62,
    SYS_fsync = 74, SYS_rmdir = 84, SYS_rename = 82, SYS_unlink = 87, SYS_arch_prctl = 158, SYS_gettid = 186,
    SYS_futex = 202, SYS_clock_gettime = 228, SYS_clock_getres = 229, SYS_exit_group = 231,
    SYS_openat = 257, SYS_getrandom = 318,
};

static inline long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}
#define sc1(n, a) sc6(n, (long)(a), 0, 0, 0, 0, 0)
#define sc2(n, a, b) sc6(n, (long)(a), (long)(b), 0, 0, 0, 0)
#define sc3(n, a, b, c) sc6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define sc4(n, a, b, c, d) sc6(n, (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)

/* Kernel returns -errno in [-4095, -1]; libc23 uses the same errno numbers. */
static inline int failed(long r) { return (unsigned long)r > (unsigned long)-4096L; }
static inline int err(long r) { return (int)r; }

/* ---------------------------------------------------------------------- */
/* Identification, process                                                 */
/* ---------------------------------------------------------------------- */
const char pal_name[] = "linux-x86_64";
const char pal_path_sep = '/';
char **pal_environ;

_Noreturn void pal_exit(int status) {
    for (;;) sc1(SYS_exit_group, status);
}

struct ksigaction {
    void (*handler)(int);
    unsigned long flags;
    void (*restorer)(void);
    unsigned long mask;
};

_Noreturn void pal_abort(void) {
    struct ksigaction sa = {0, 0, 0, 0}; /* SIG_DFL */
    sc4(SYS_rt_sigaction, 6, &sa, 0, 8);
    sc2(SYS_kill, sc1(SYS_getpid, 0), 6);
    pal_exit(134);
}

int pal_system(const char *command, int *status) {
    if (!command) {
        *status = failed(sc2(SYS_access, "/bin/sh", 1)) ? 0 : 1;
        return 0;
    }
    long pid = sc1(SYS_fork, 0);
    if (failed(pid)) return err(pid);
    if (pid == 0) {
        const char *argv[] = {"sh", "-c", command, 0};
        sc3(SYS_execve, "/bin/sh", argv, pal_environ);
        for (;;) sc1(SYS_exit, 127);
    }
    int st = 0;
    long r;
    do r = sc4(SYS_wait4, pid, &st, 0, 0);
    while (r == -EINTR);
    if (failed(r)) return err(r);
    *status = (st & 0x7f) == 0 ? (st >> 8) & 0xff : 128 + (st & 0x7f);
    return 0;
}

int pal_random(void *buf, size_t n) {
    unsigned char *p = buf;
    while (n) {
        long r = sc3(SYS_getrandom, p, n, 0);
        if (r == -EINTR) continue;
        if (failed(r)) return err(r);
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Memory                                                                  */
/* ---------------------------------------------------------------------- */
size_t pal_page_size(void) { return 4096; }

void *pal_mem_map(size_t size) {
    long r = sc6(SYS_mmap, 0, (long)size, 3 /* RW */, 0x22 /* PRIVATE|ANONYMOUS */, -1, 0);
    return failed(r) ? NULL : (void *)r;
}

int pal_mem_unmap(void *ptr, size_t size) {
    long r = sc2(SYS_munmap, ptr, size);
    return failed(r) ? err(r) : 0;
}

/* ---------------------------------------------------------------------- */
/* Files                                                                   */
/* ---------------------------------------------------------------------- */
int pal_open(const char *path, int flags, pal_file_t *out) {
    int acc = flags & 3;
    long f = acc == PAL_O_RDONLY ? 0 : acc == PAL_O_WRONLY ? 1 : 2;
    f |= 02000000; /* O_CLOEXEC */
    if (flags & PAL_O_CREAT) f |= 0100;
    if (flags & PAL_O_EXCL) f |= 0200;
    if (flags & PAL_O_TRUNC) f |= 01000;
    if (flags & PAL_O_APPEND) f |= 02000;
    long fd = sc4(SYS_openat, -100 /* AT_FDCWD */, path, f, 0666);
    if (failed(fd)) return err(fd);
    if (flags & PAL_O_TEMP) sc1(SYS_unlink, path);
    *out = (pal_file_t)fd;
    return 0;
}

int pal_close(pal_file_t f) {
    long r = sc1(SYS_close, f);
    return failed(r) ? err(r) : 0;
}

int pal_read(pal_file_t f, void *buf, size_t n, size_t *got) {
    long r;
    do r = sc3(SYS_read, f, buf, n);
    while (r == -EINTR);
    if (failed(r)) return err(r);
    *got = (size_t)r;
    return 0;
}

int pal_write(pal_file_t f, const void *buf, size_t n, size_t *written) {
    long r;
    do r = sc3(SYS_write, f, buf, n);
    while (r == -EINTR);
    if (failed(r)) return err(r);
    *written = (size_t)r;
    return 0;
}

int pal_seek(pal_file_t f, int64_t off, int whence, int64_t *newpos) {
    long r = sc3(SYS_lseek, f, off, whence);
    if (failed(r)) return err(r);
    if (newpos) *newpos = r;
    return 0;
}

int pal_sync(pal_file_t f) {
    long r = sc1(SYS_fsync, f);
    return failed(r) && r != -EINVAL && r != -EROFS ? err(r) : 0;
}

int pal_isatty(pal_file_t f) {
    char termios[64];
    return !failed(sc3(SYS_ioctl, f, 0x5401 /* TCGETS */, termios)) ? 1 : 0;
}

pal_file_t pal_std_file(int n) { return n >= 0 && n <= 2 ? (pal_file_t)n : PAL_INVALID_FILE; }

int pal_remove(const char *path) {
    long r = sc1(SYS_unlink, path);
    if (r == -EISDIR || r == -EPERM) r = sc1(SYS_rmdir, path);
    return failed(r) ? err(r) : 0;
}

int pal_rename(const char *from, const char *to) {
    long r = sc2(SYS_rename, from, to);
    return failed(r) ? err(r) : 0;
}

static const char *env_get(const char *name) {
    if (!pal_environ) return NULL;
    size_t nl = 0;
    while (name[nl]) nl++;
    for (char **e = pal_environ; *e; e++) {
        size_t i = 0;
        while (i < nl && (*e)[i] == name[i]) i++;
        if (i == nl && (*e)[nl] == '=') return *e + nl + 1;
    }
    return NULL;
}

int pal_tmpdir(char *buf, size_t n) {
    const char *t = env_get("TMPDIR");
    if (!t || !*t) t = "/tmp";
    size_t l = 0;
    while (t[l]) l++;
    int slash = t[l - 1] != '/';
    if (l + (size_t)slash + 1 > n) return -ERANGE;
    for (size_t i = 0; i < l; i++) buf[i] = t[i];
    if (slash) buf[l++] = '/';
    buf[l] = 0;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Time                                                                    */
/* ---------------------------------------------------------------------- */
struct ktimespec {
    long sec, nsec;
};

int pal_clock_gettime(int clock, struct pal_timespec *ts) {
    struct ktimespec k;
    long r = sc2(SYS_clock_gettime, clock, &k);
    if (failed(r)) return err(r);
    ts->sec = k.sec;
    ts->nsec = (int32_t)k.nsec;
    return 0;
}

int pal_clock_getres(int clock, struct pal_timespec *ts) {
    struct ktimespec k;
    long r = sc2(SYS_clock_getres, clock, &k);
    if (failed(r)) return err(r);
    ts->sec = k.sec;
    ts->nsec = (int32_t)k.nsec;
    return 0;
}

int pal_sleep(const struct pal_timespec *d) {
    struct ktimespec k = {d->sec, d->nsec};
    long r = sc2(SYS_nanosleep, &k, 0);
    return failed(r) ? err(r) : 0;
}

/* Deterministic on every host: results must not depend on /etc/localtime. */
int pal_tz_lookup(int64_t utc, struct pal_tzinfo *out) {
    (void)utc;
    out->utc_offset = 0;
    out->is_dst = 0;
    out->abbr[0] = 'U';
    out->abbr[1] = 'T';
    out->abbr[2] = 'C';
    out->abbr[3] = 0;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Futex                                                                   */
/* ---------------------------------------------------------------------- */
#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

int pal_futex_wait(volatile int32_t *addr, int32_t expected, int64_t timeout_ns) {
    struct ktimespec k;
    struct ktimespec *kp = 0;
    if (timeout_ns >= 0) {
        k.sec = timeout_ns / 1000000000LL;
        k.nsec = timeout_ns % 1000000000LL;
        kp = &k;
    }
    long r = sc4(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, kp);
    if (r == -ETIMEDOUT) return -ETIMEDOUT;
    return 0; /* woken, EAGAIN (value changed) and EINTR are all "re-check" */
}

void pal_futex_wake(volatile int32_t *addr, int32_t count) { sc3(SYS_futex, addr, FUTEX_WAKE_PRIVATE, count); }

/* ---------------------------------------------------------------------- */
/* Threads (clone + futex). TLS image information comes from start.c.      */
/* ---------------------------------------------------------------------- */
extern const unsigned char *__pal_tls_image;
extern size_t __pal_tls_filesz, __pal_tls_memsz, __pal_tls_align;

struct trec {
    volatile int32_t tid;   /* set by the kernel at clone, cleared at exit (futex-woken) */
    volatile int32_t state; /* 0 running/joinable, 1 detached, 2 finished */
    void (*fn)(void *);
    void *arg;
    void *base;
    size_t size;
};

/* Lays out the TLS block of a thread inside [area, area+len); returns the thread pointer.
 * x86-64 variant II: the block ends exactly at the thread pointer. */
void *__pal_tls_init(char *area, size_t len) {
    size_t ta = __pal_tls_align ? __pal_tls_align : 1;
    size_t tls = (__pal_tls_memsz + ta - 1) & ~(ta - 1);
    size_t al = ta > 64 ? ta : 64;
    uintptr_t tp = ((uintptr_t)area + tls + al - 1) & ~(uintptr_t)(al - 1);
    (void)len;
    char *blk = (char *)tp - tls;
    for (size_t i = 0; i < __pal_tls_filesz; i++) blk[i] = (char)__pal_tls_image[i];
    for (size_t i = __pal_tls_filesz; i < tls; i++) blk[i] = 0;
    *(void **)tp = (void *)tp; /* fs:0 must hold the thread pointer */
    ((void **)tp)[1] = 0;
    return (void *)tp;
}

size_t __pal_tls_area_size(void) {
    size_t ta = __pal_tls_align ? __pal_tls_align : 1;
    size_t al = ta > 64 ? ta : 64;
    return ((__pal_tls_memsz + ta - 1) & ~(ta - 1)) + al + 64;
}

long __pal_clone(unsigned long flags, void *stack, int *ptid, int *ctid, void *tls, void (*entry)(void *), void *arg);
_Noreturn void __pal_exit_unmap(void *base, size_t size);

__asm__(".text\n"
        ".globl __pal_clone\n"
        "__pal_clone:\n"
        "  mov %rcx, %r10\n"
        "  mov 8(%rsp), %rax\n"
        "  and $-16, %rsi\n"
        "  sub $16, %rsi\n"
        "  mov %r9, (%rsi)\n"
        "  mov %rax, 8(%rsi)\n"
        "  mov $56, %eax\n"
        "  syscall\n"
        "  test %rax, %rax\n"
        "  jnz 1f\n"
        "  xor %ebp, %ebp\n"
        "  pop %rax\n"
        "  pop %rdi\n"
        "  call *%rax\n"
        "  mov $60, %eax\n"
        "  xor %edi, %edi\n"
        "  syscall\n"
        "1:\n"
        "  ret\n"
        ".globl __pal_exit_unmap\n"
        "__pal_exit_unmap:\n"
        "  mov $11, %eax\n"
        "  syscall\n"
        "  mov $60, %eax\n"
        "  xor %edi, %edi\n"
        "  syscall\n");

static _Thread_local struct trec *tls_self;

static void thread_finish(struct trec *t) {
    __libc_tss_run_dtors();
    int32_t old = __atomic_exchange_n(&t->state, 2, __ATOMIC_SEQ_CST);
    if (old == 1) __pal_exit_unmap(t->base, t->size); /* detached: nobody will join, free ourselves */
    for (;;) sc1(SYS_exit, 0); /* joinable: the kernel clears t->tid and wakes the joiner */
}

static void thread_entry(void *p) {
    struct trec *t = p;
    tls_self = t;
    t->fn(t->arg);
    thread_finish(t);
}

int pal_thread_create(void (*fn)(void *), void *arg, size_t stack_size, pal_thread_t *out) {
    size_t ps = 4096;
    if (!stack_size) stack_size = 1u << 20;
    stack_size = (stack_size + ps - 1) & ~(ps - 1);
    size_t tls_area = (__pal_tls_area_size() + ps - 1) & ~(ps - 1);
    size_t size = ps /* guard */ + stack_size + tls_area + ps /* record */;
    char *base = pal_mem_map(size);
    if (!base) return -EAGAIN;
    sc3(SYS_mprotect, base, ps, 0); /* guard page */
    struct trec *t = (struct trec *)(base + size - sizeof *t);
    t->fn = fn;
    t->arg = arg;
    t->base = base;
    t->size = size;
    char *tls_start = base + size - ps;
    void *tp = __pal_tls_init(tls_start - tls_area, tls_area);
    char *stack_top = tls_start - tls_area;
    unsigned long flags = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x80000 | 0x100000 | 0x200000;
    long r = __pal_clone(flags, stack_top, (int *)&t->tid, (int *)&t->tid, tp, thread_entry, t);
    if (failed(r)) {
        pal_mem_unmap(base, size);
        return err(r);
    }
    *out = (pal_thread_t)t;
    return 0;
}

int pal_thread_join(pal_thread_t h) {
    struct trec *t = (struct trec *)h;
    int32_t v;
    /* the kernel wakes CLONE_CHILD_CLEARTID with a shared (non-private) futex key */
    while ((v = t->tid) != 0) sc4(SYS_futex, &t->tid, 0 /* FUTEX_WAIT */, v, 0);
    pal_mem_unmap(t->base, t->size);
    return 0;
}

int pal_thread_detach(pal_thread_t h) {
    struct trec *t = (struct trec *)h;
    int32_t expect = 0;
    if (__atomic_compare_exchange_n(&t->state, &expect, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
    /* already finished: wait for the kernel to be done with the stack, then release it */
    return pal_thread_join(h);
}

_Noreturn void pal_thread_exit(void) {
    struct trec *t = tls_self;
    if (!t) pal_exit(0); /* main thread */
    thread_finish(t);
    for (;;) sc1(SYS_exit, 0);
}

pal_tid_t pal_thread_self(void) { return (pal_tid_t)sc1(SYS_gettid, 0); }

void pal_thread_yield(void) { sc1(SYS_sched_yield, 0); }

/* ---------------------------------------------------------------------- */
/* Signals                                                                 */
/* ---------------------------------------------------------------------- */
static void signal_trampoline(int sig) {
    __libc_signal_dispatch(sig);
    if (sig == 4 || sig == 8 || sig == 11) pal_abort();
}

__asm__(".text\n"
        ".globl __pal_sigreturn\n"
        "__pal_sigreturn:\n"
        "  mov $15, %eax\n"
        "  syscall\n");
extern void __pal_sigreturn(void);

int pal_signal_enable(int sig, int on) {
    if (sig != 2 && sig != 4 && sig != 6 && sig != 8 && sig != 11 && sig != 15) return -EINVAL;
    struct ksigaction sa;
    sa.handler = on ? signal_trampoline : 0;
    sa.flags = 0x04000000 /* SA_RESTORER */ | 0x40000000 /* SA_NODEFER */;
    sa.restorer = __pal_sigreturn;
    sa.mask = 0;
    long r = sc4(SYS_rt_sigaction, sig, &sa, 0, 8);
    return failed(r) ? err(r) : 0;
}
