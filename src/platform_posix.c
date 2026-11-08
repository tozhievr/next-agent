/* NextAgent platform layer for Linux x86-64 (statically linked against libc23).
 * Raw system calls only, so the program needs neither glibc nor musl. Behaviour matches the Windows
 * layer (platform_win.c): same return codes, '/'-separated normalized paths, sorted directory listings. */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "platform_posix.c supports x86-64 Linux only"
#endif

enum {
    SYS_read = 0, SYS_write = 1, SYS_close = 3, SYS_lseek = 8, SYS_rt_sigaction = 13, SYS_poll = 7,
    SYS_access = 21, SYS_nanosleep = 35, SYS_getpid = 39, SYS_fork = 57, SYS_execve = 59, SYS_exit = 60,
    SYS_wait4 = 61, SYS_kill = 62, SYS_fsync = 74, SYS_ftruncate = 77, SYS_getcwd = 79, SYS_chdir = 80,
    SYS_rename = 82, SYS_unlink = 87, SYS_flock = 73, SYS_dup2 = 33, SYS_clock_gettime = 228,
    SYS_getdents64 = 217, SYS_openat = 257, SYS_mkdirat = 258, SYS_newfstatat = 262, SYS_pipe2 = 293,
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

static inline bool bad(long r) { return (unsigned long)r > (unsigned long)-4096L; }

#define E_INTR 4
#define E_NOENT 2
#define E_NOTDIR 20
#define E_EXIST 17
#define E_AGAIN 11
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_DIRECTORY 0200000
#define O_CLOEXEC 02000000

extern char **__libc_pal_environ; /* libc23 PAL: environment handed over by _start */

static long open_path(const char *path, long flags, long mode) { return sc4(SYS_openat, AT_FDCWD, path, flags | O_CLOEXEC, mode); }

/* ---- stat ---- */
static bool stat_mode(const char *path, bool follow, unsigned *mode) {
    unsigned char st[144];
    long r = sc4(SYS_newfstatat, AT_FDCWD, path, st, follow ? 0 : AT_SYMLINK_NOFOLLOW);
    if (bad(r)) return false;
    memcpy(mode, st + 24, 4);
    return true;
}

bool pf_is_file(const char *path) {
    unsigned m;
    return stat_mode(path, true, &m) && (m & 0170000) == 0100000;
}

bool pf_is_dir(const char *path) {
    unsigned m;
    return stat_mode(path, true, &m) && (m & 0170000) == 0040000;
}

bool pf_is_reparse(const char *path) {
    unsigned m;
    return stat_mode(path, false, &m) && (m & 0170000) == 0120000;
}

/* ---- reading ---- */
int pf_read_from(const char *path, uint64_t offset, Buf *out) {
    long fd = open_path(path, O_RDONLY, 0);
    if (bad(fd)) {
        if (fd == -E_NOENT || fd == -E_NOTDIR) return 1;
        return na_fail("cannot open %s (error %ld)", path, -fd);
    }
    if (offset && bad(sc3(SYS_lseek, fd, offset, 0))) {
        sc1(SYS_close, fd);
        return na_fail("cannot read %s", path);
    }
    char chunk[65536];
    for (;;) {
        long n = sc3(SYS_read, fd, chunk, sizeof chunk);
        if (n == -E_INTR) continue;
        if (bad(n)) {
            sc1(SYS_close, fd);
            return na_fail("cannot read %s", path);
        }
        if (n == 0) break;
        buf_put(out, chunk, (size_t)n);
    }
    sc1(SYS_close, fd);
    return 0;
}

int pf_read_file(const char *path, Buf *out) { return pf_read_from(path, 0, out); }

/* ---- writing ---- */
static int write_all(long fd, const void *data, size_t n) {
    const char *p = data;
    while (n > 0) {
        long w = sc3(SYS_write, fd, p, n);
        if (w == -E_INTR) continue;
        if (bad(w) || w == 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

static int make_dir(const char *path) {
    long r = sc3(SYS_mkdirat, AT_FDCWD, path, 0777);
    return (bad(r) && r != -E_EXIST) ? -1 : 0;
}

int pf_mkdirs(const char *dir) {
    if (pf_is_dir(dir)) return 0;
    char *copy = na_strdup(dir);
    for (size_t i = 1; copy[i]; i++) {
        if (copy[i] != '/') continue;
        copy[i] = 0;
        if (!pf_is_dir(copy) && make_dir(copy) != 0) {
            int rc = na_fail("cannot create directory %s", copy);
            free(copy);
            return rc;
        }
        copy[i] = '/';
    }
    free(copy);
    if (make_dir(dir) != 0) return na_fail("cannot create directory %s", dir);
    return 0;
}

int pf_write_atomic(const char *path, const void *data, size_t n) {
    static unsigned counter = 0;
    const char *slash = strrchr(path, '/');
    size_t dirlen = slash ? (size_t)(slash - path) : 0;
    Buf tmp;
    buf_init(&tmp);
    if (dirlen) {
        buf_put(&tmp, path, dirlen);
        buf_putc(&tmp, '/');
    }
    buf_printf(&tmp, ".aed-%u-%llu-%u.tmp", (unsigned)sc1(SYS_getpid, 0), (unsigned long long)pf_now_ms(), counter++);
    long fd = open_path(tmp.p, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (bad(fd)) {
        buf_free(&tmp);
        return na_fail("cannot create temporary file next to %s", path);
    }
    int rc = write_all(fd, data, n);
    if (rc == 0 && bad(sc1(SYS_fsync, fd))) rc = -1;
    sc1(SYS_close, fd);
    if (rc != 0) {
        sc1(SYS_unlink, tmp.p);
        buf_free(&tmp);
        return na_fail("cannot write temporary file for %s", path);
    }
    long r = sc2(SYS_rename, tmp.p, path);
    if (bad(r)) {
        sc1(SYS_unlink, tmp.p);
        buf_free(&tmp);
        return na_fail("cannot replace %s (error %ld)", path, -r);
    }
    buf_free(&tmp);
    return 0;
}

int pf_append_at(const char *path, uint64_t at, const void *data, size_t n) {
    long fd = open_path(path, O_RDWR | O_CREAT, 0666);
    if (bad(fd)) return na_fail("cannot open journal %s", path);
    int rc = 0;
    if (bad(sc2(SYS_ftruncate, fd, at)) || bad(sc3(SYS_lseek, fd, at, 0))) rc = -1;
    if (rc == 0) rc = write_all(fd, data, n);
    if (rc == 0 && bad(sc1(SYS_fsync, fd))) rc = -1;
    sc1(SYS_close, fd);
    if (rc != 0) return na_fail("cannot append to journal %s", path);
    return 0;
}

int pf_remove(const char *path) { return bad(sc1(SYS_unlink, path)) ? -1 : 0; }

char *pf_fullpath(const char *path) {
    if (path[0] == '/' || path[0] == '\\') return path_normalize(path);
    char cwd[4096];
    long r = sc2(SYS_getcwd, cwd, sizeof cwd);
    if (bad(r)) return NULL;
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", cwd, path);
    char *norm = path_normalize(b.p);
    buf_free(&b);
    return norm;
}

/* ---- directories (sorted, so listings are identical on every platform) ---- */
int pf_list_dir(const char *dir, StrList *files, StrList *dirs) {
    long fd = open_path(dir, O_RDONLY | O_DIRECTORY, 0);
    if (bad(fd)) return na_fail("cannot list %s", dir);
    StrList f, d;
    sl_init(&f);
    sl_init(&d);
    char buf[16384];
    for (;;) {
        long n = sc3(SYS_getdents64, fd, buf, sizeof buf);
        if (bad(n)) {
            sc1(SYS_close, fd);
            sl_free(&f);
            sl_free(&d);
            return na_fail("cannot list %s", dir);
        }
        if (n == 0) break;
        for (long off = 0; off < n;) {
            unsigned short reclen;
            memcpy(&reclen, buf + off + 16, 2);
            unsigned char type = (unsigned char)buf[off + 18];
            const char *name = buf + off + 19;
            off += reclen;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            if (type == 10) continue; /* DT_LNK: links are skipped like reparse points */
            Buf p;
            buf_init(&p);
            buf_printf(&p, "%s/%s", dir, name);
            if (type == 0) { /* DT_UNKNOWN */
                unsigned m;
                if (!stat_mode(p.p, false, &m) || (m & 0170000) == 0120000) {
                    buf_free(&p);
                    continue;
                }
                type = (m & 0170000) == 0040000 ? 4 : 8;
            }
            buf_free(&p);
            if (type == 4) sl_push(&d, name);
            else if (type == 8) sl_push(&f, name);
        }
    }
    sc1(SYS_close, fd);
    sl_sort(&f);
    sl_sort(&d);
    for (size_t i = 0; i < f.n; i++)
        if (files) sl_push(files, f.v[i]);
    for (size_t i = 0; i < d.n; i++)
        if (dirs) sl_push(dirs, d.v[i]);
    sl_free(&f);
    sl_free(&d);
    return 0;
}

/* ---- cross-process lock: flock, released by the kernel even if the holder dies ---- */
struct PfLock {
    long fd;
};

PfLock *pf_lock_acquire(const char *path, int timeout_ms) {
    long fd = open_path(path, O_RDWR | O_CREAT, 0666);
    if (bad(fd)) {
        na_seterr("store_locked: cannot open lock file %s", path);
        return NULL;
    }
    uint64_t deadline = pf_now_ms() + (uint64_t)timeout_ms;
    for (;;) {
        long r = sc2(SYS_flock, fd, 2 | 4); /* LOCK_EX | LOCK_NB */
        if (!bad(r)) {
            PfLock *l = na_alloc(sizeof *l);
            l->fd = fd;
            return l;
        }
        if (r != -E_AGAIN && r != -E_INTR) break;
        if (pf_now_ms() >= deadline) break;
        pf_sleep_ms(5);
    }
    sc1(SYS_close, fd);
    na_seterr("store_locked: another process holds the workspace lock");
    return NULL;
}

void pf_lock_release(PfLock *lock) {
    if (!lock) return;
    sc1(SYS_close, lock->fd); /* closing drops the flock */
    free(lock);
}

/* ---- time ---- */
uint64_t pf_now_ms(void) {
    long ts[2];
    sc2(SYS_clock_gettime, 1 /* MONOTONIC */, ts);
    return (uint64_t)ts[0] * 1000u + (uint64_t)ts[1] / 1000000u;
}

void pf_sleep_ms(unsigned ms) {
    long ts[2] = {ms / 1000, (long)(ms % 1000) * 1000000L};
    sc2(SYS_nanosleep, ts, 0);
}

/* ---- child processes ---- */
struct PfProc {
    long pid, in_w, out_r;
    bool exited;
};

static bool executable_file(const char *p) { return pf_is_file(p) && !bad(sc2(SYS_access, p, 1)); }

char *pf_which(const char *exe) {
    if (strchr(exe, '/')) return executable_file(exe) ? pf_fullpath(exe) : NULL;
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";
    const char *p = path;
    while (true) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        Buf b;
        buf_init(&b);
        if (n == 0) buf_puts(&b, ".");
        else buf_put(&b, p, n);
        buf_printf(&b, "/%s", exe);
        bool ok = executable_file(b.p);
        char *full = ok ? pf_fullpath(b.p) : NULL;
        buf_free(&b);
        if (full) return full;
        if (!e) return NULL;
        p = e + 1;
    }
}

/* Splits a command-line tail into words; double quotes group, no other escapes. */
static char **split_args(const char *full, const char *args, size_t *count) {
    size_t cap = 8, n = 0;
    char **v = na_alloc(cap * sizeof *v);
    v[n++] = na_strdup(full);
    const char *p = args ? args : "";
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        Buf w;
        buf_init(&w);
        bool q = false;
        while (*p && (q || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') q = !q;
            else buf_putc(&w, *p);
            p++;
        }
        if (n + 2 > cap) {
            cap *= 2;
            v = na_realloc(v, cap * sizeof *v);
        }
        v[n++] = buf_take(&w);
    }
    v[n] = NULL;
    *count = n;
    return v;
}

PfProc *pf_spawn(const char *exe, const char *args, const char *cwd) {
    char *full = pf_which(exe);
    if (!full) {
        na_seterr("executable not found: %s", exe);
        return NULL;
    }
    int in_p[2], out_p[2];
    if (bad(sc2(SYS_pipe2, in_p, O_CLOEXEC)) || bad(sc2(SYS_pipe2, out_p, O_CLOEXEC))) {
        free(full);
        na_seterr("cannot create pipes");
        return NULL;
    }
    static bool sigpipe_ignored;
    if (!sigpipe_ignored) { /* a dead server must surface as a write error, not kill us */
        unsigned long sa[4] = {1 /* SIG_IGN */, 0, 0, 0};
        sc4(SYS_rt_sigaction, 13, sa, 0, 8);
        sigpipe_ignored = true;
    }
    size_t argc;
    char **argv = split_args(full, args, &argc);
    long pid = sc1(SYS_fork, 0);
    if (pid == 0) { /* child: only async-signal-safe raw calls */
        sc2(SYS_dup2, in_p[0], 0);
        sc2(SYS_dup2, out_p[1], 1);
        long nul = open_path("/dev/null", O_WRONLY, 0);
        if (!bad(nul)) sc2(SYS_dup2, nul, 2);
        if (cwd) sc1(SYS_chdir, cwd);
        sc3(SYS_execve, full, argv, __libc_pal_environ);
        for (;;) sc1(SYS_exit, 127);
    }
    for (size_t i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    free(full);
    sc1(SYS_close, in_p[0]);
    sc1(SYS_close, out_p[1]);
    if (bad(pid)) {
        sc1(SYS_close, in_p[1]);
        sc1(SYS_close, out_p[0]);
        na_seterr("cannot start %s (error %ld)", exe, -pid);
        return NULL;
    }
    PfProc *p = na_alloc(sizeof *p);
    p->pid = pid;
    p->in_w = in_p[1];
    p->out_r = out_p[0];
    return p;
}

int pf_proc_write(PfProc *p, const void *data, size_t n) {
    if (!p || p->in_w < 0) return -1;
    return write_all(p->in_w, data, n) == 0 ? 0 : -1;
}

bool pf_proc_alive(PfProc *p) {
    if (!p || p->exited) return false;
    int st;
    long r = sc4(SYS_wait4, p->pid, &st, 1 /* WNOHANG */, 0);
    if (r == p->pid || (bad(r) && r != -E_INTR)) {
        p->exited = true;
        return false;
    }
    return true;
}

int pf_proc_read(PfProc *p, void *buf, size_t cap, int timeout_ms) {
    if (!p || p->out_r < 0) return -1;
    struct {
        int fd;
        short events, revents;
    } pfd = {(int)p->out_r, 1 /* POLLIN */, 0};
    long r = sc3(SYS_poll, &pfd, 1, timeout_ms < 0 ? 0 : timeout_ms);
    if (r == -E_INTR) return 0;
    if (bad(r)) return -1;
    if (r == 0) return 0;
    long n = sc3(SYS_read, p->out_r, buf, cap);
    if (n == -E_INTR) return 0;
    if (bad(n) || n == 0) return -1; /* EOF: the server closed its stdout */
    return (int)n;
}

void pf_proc_close(PfProc *p, int graceful_ms) {
    if (!p) return;
    if (p->in_w >= 0) sc1(SYS_close, p->in_w);
    p->in_w = -1;
    if (!p->exited) {
        uint64_t deadline = pf_now_ms() + (uint64_t)(graceful_ms < 0 ? 0 : graceful_ms);
        while (pf_proc_alive(p) && pf_now_ms() < deadline) pf_sleep_ms(2);
        if (!p->exited) {
            sc2(SYS_kill, p->pid, 9);
            int st;
            sc4(SYS_wait4, p->pid, &st, 0, 0);
        }
    }
    if (p->out_r >= 0) sc1(SYS_close, p->out_r);
    free(p);
}

/* ---- TCP client (raw socket syscalls) ---- */
struct PfSock {
    int fd;
};

static bool parse_ipv4(const char *host, uint8_t out[4]) {
    if (strcmp(host, "localhost") == 0) host = "127.0.0.1";
    const char *p = host;
    for (int i = 0; i < 4; i++) {
        if (*p < '0' || *p > '9') return false;
        unsigned v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
        if (v > 255 || (i < 3 && *p++ != '.')) return false;
        out[i] = (uint8_t)v;
    }
    return *p == 0;
}

PfSock *pf_tcp_connect(const char *host, int port, int timeout_ms) {
    uint8_t ip[4];
    if (!parse_ipv4(host, ip)) {
        na_seterr("embedding url: host must be an IPv4 address or localhost (got %s)", host);
        return NULL;
    }
    long fd = sc3(41 /* socket */, 2, 1 | 02000000 /* SOCK_STREAM | SOCK_CLOEXEC */, 6);
    if (bad(fd)) {
        na_seterr("cannot create socket");
        return NULL;
    }
    long tv[2] = {timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000};
    sc6(54 /* setsockopt */, fd, 1 /* SOL_SOCKET */, 21 /* SO_SNDTIMEO */, (long)tv, sizeof tv, 0);
    unsigned char sa[16] = {2, 0, (unsigned char)(port >> 8), (unsigned char)port, ip[0], ip[1], ip[2], ip[3]};
    if (bad(sc3(42 /* connect */, fd, sa, sizeof sa))) {
        sc1(SYS_close, fd);
        na_seterr("cannot connect to %s:%d", host, port);
        return NULL;
    }
    PfSock *k = na_alloc(sizeof *k);
    k->fd = (int)fd;
    return k;
}

int pf_sock_write(PfSock *k, const void *data, size_t n) {
    const char *p = data;
    while (n > 0) {
        long w = sc6(44 /* sendto */, k->fd, (long)p, (long)n, 0x4000 /* MSG_NOSIGNAL */, 0, 0);
        if (w == -E_INTR) continue;
        if (bad(w) || w == 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

int pf_sock_read(PfSock *k, void *buf, size_t cap, int timeout_ms) {
    struct {
        int fd;
        short events, revents;
    } pfd = {k->fd, 1 /* POLLIN */, 0};
    long pr = sc3(SYS_poll, &pfd, 1, timeout_ms < 0 ? 0 : timeout_ms);
    if (pr == -E_INTR) return 0;
    if (bad(pr)) return -1;
    if (pr == 0) return 0;
    long r = sc6(45 /* recvfrom */, k->fd, (long)buf, (long)cap, 0, 0, 0);
    if (r == -E_INTR || r == -E_AGAIN) return 0;
    return bad(r) || r == 0 ? -1 : (int)r;
}

void pf_sock_close(PfSock *k) {
    if (!k) return;
    sc1(SYS_close, k->fd);
    free(k);
}

/* ---- cheap change detection ---- */
bool pf_stat(const char *path, PfStat *out) {
    unsigned char st[144];
    if (bad(sc4(SYS_newfstatat, AT_FDCWD, path, st, 0))) return false;
    unsigned mode;
    long size, sec, nsec;
    memcpy(&mode, st + 24, 4);
    memcpy(&size, st + 48, 8);
    memcpy(&sec, st + 88, 8);
    memcpy(&nsec, st + 96, 8);
    if ((mode & 0170000) != 0100000) return false;
    out->size = (uint64_t)size;
    out->mtime_ms = (int64_t)sec * 1000 + nsec / 1000000;
    return true;
}

int64_t pf_unix_ms(void) {
    long ts[2];
    sc2(SYS_clock_gettime, 0 /* REALTIME */, ts);
    return (int64_t)ts[0] * 1000 + ts[1] / 1000000;
}
