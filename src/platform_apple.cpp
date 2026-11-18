/* macOS platform layer. Same return codes and sorted listings as the Linux
 * syscall layer, implemented with the system libc so arm64 and x86-64 share it. */
#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <mach-o/dyld.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__APPLE__)
#error "platform_apple.cpp supports macOS only"
#endif

extern "C" char **environ;

static bool interrupted(int value) { return value < 0 && errno == EINTR; }

static int open_path(const char *path, int flags, int mode) {
    for (;;) {
        int fd = open(path, flags | O_CLOEXEC, mode);
        if (fd >= 0 || errno != EINTR) return fd;
    }
}

static int write_all(int fd, const void *data, size_t n) {
    const char *p = static_cast<const char *>(data);
    while (n > 0) {
        ssize_t wrote = write(fd, p, n);
        if (interrupted(wrote)) continue;
        if (wrote <= 0) return -1;
        p += wrote;
        n -= (size_t)wrote;
    }
    return 0;
}

bool pf_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool pf_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool pf_is_reparse(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

int pf_read_from(const char *path, uint64_t offset, Buf *out) {
    int fd = open_path(path, O_RDONLY, 0);
    if (fd < 0) return errno == ENOENT || errno == ENOTDIR ? 1 : na_fail("cannot open %s (error %d)", path, errno);
    if (offset && lseek(fd, (off_t)offset, SEEK_SET) < 0) {
        close(fd);
        return na_fail("cannot read %s", path);
    }
    char chunk[65536];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (interrupted(n)) continue;
        if (n < 0) {
            close(fd);
            return na_fail("cannot read %s", path);
        }
        if (n == 0) break;
        buf_put(out, chunk, (size_t)n);
    }
    close(fd);
    return 0;
}

int pf_read_file(const char *path, Buf *out) { return pf_read_from(path, 0, out); }

static int make_dir(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return 0;
    return -1;
}

int pf_mkdirs(const char *dir) {
    if (pf_is_dir(dir)) return 0;
    char *copy = na_strdup(dir);
    for (size_t i = 1; copy[i]; i++) {
        if (copy[i] != '/') continue;
        copy[i] = 0;
        if (!pf_is_dir(copy) && make_dir(copy) != 0) {
            free(copy);
            return na_fail("cannot create directory %s", dir);
        }
        copy[i] = '/';
    }
    int rc = pf_is_dir(copy) || make_dir(copy) == 0 ? 0 : na_fail("cannot create directory %s", dir);
    free(copy);
    return rc;
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
    buf_printf(&tmp, ".aed-%u-%llu-%u.tmp", (unsigned)getpid(), (unsigned long long)pf_now_ms(), counter++);
    int fd = open_path(tmp.p, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        buf_free(&tmp);
        return na_fail("cannot create temporary file next to %s", path);
    }
    int rc = write_all(fd, data, n);
    if (rc == 0 && fsync(fd) != 0) rc = -1;
    close(fd);
    if (rc != 0 || rename(tmp.p, path) != 0) {
        unlink(tmp.p);
        buf_free(&tmp);
        return na_fail("cannot replace %s", path);
    }
    buf_free(&tmp);
    return 0;
}

int pf_append_at(const char *path, uint64_t at, const void *data, size_t n) {
    int fd = open_path(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return na_fail("cannot open journal %s", path);
    int rc = 0;
    if (ftruncate(fd, (off_t)at) != 0 || lseek(fd, (off_t)at, SEEK_SET) < 0) rc = -1;
    if (rc == 0) rc = write_all(fd, data, n);
    if (rc == 0 && fsync(fd) != 0) rc = -1;
    close(fd);
    return rc == 0 ? 0 : na_fail("cannot append to journal %s", path);
}

int pf_remove(const char *path) { return unlink(path) == 0 ? 0 : -1; }

char *pf_fullpath(const char *path) {
    if (path[0] == '/' || path[0] == '\\') return path_normalize(path);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return NULL;
    Buf b;
    buf_init(&b);
    buf_printf(&b, "%s/%s", cwd, path);
    char *norm = path_normalize(b.p);
    buf_free(&b);
    return norm;
}

int pf_list_dir(const char *dir, StrList *files, StrList *dirs) {
    DIR *handle = opendir(dir);
    if (!handle) return na_fail("cannot list %s", dir);
    StrList f, d;
    sl_init(&f);
    sl_init(&d);
    for (;;) {
        errno = 0;
        dirent *entry = readdir(handle);
        if (!entry) {
            if (errno) {
                closedir(handle);
                sl_free(&f);
                sl_free(&d);
                return na_fail("cannot list %s", dir);
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        Buf p;
        buf_init(&p);
        buf_printf(&p, "%s/%s", dir, entry->d_name);
        struct stat st;
        bool linked = lstat(p.p, &st) != 0 || S_ISLNK(st.st_mode);
        bool directory = !linked && S_ISDIR(st.st_mode);
        bool regular = !linked && S_ISREG(st.st_mode);
        buf_free(&p);
        if (directory) sl_push(&d, entry->d_name);
        else if (regular) sl_push(&f, entry->d_name);
    }
    closedir(handle);
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

struct PfLock {
    int fd;
};

PfLock *pf_lock_acquire(const char *path, int timeout_ms) {
    int fd = open_path(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        na_seterr("store_locked: cannot open lock file %s", path);
        return NULL;
    }
    uint64_t deadline = pf_now_ms() + (uint64_t)timeout_ms;
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            PfLock *lock = static_cast<PfLock *>(na_alloc(sizeof *lock));
            lock->fd = fd;
            return lock;
        }
        if (errno != EAGAIN && errno != EINTR) break;
        if (pf_now_ms() >= deadline) break;
        pf_sleep_ms(5);
    }
    close(fd);
    na_seterr("store_locked: another process holds the workspace lock");
    return NULL;
}

void pf_lock_release(PfLock *lock) {
    if (!lock) return;
    close(lock->fd);
    free(lock);
}

uint64_t pf_now_ms(void) {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

void pf_sleep_ms(unsigned ms) {
    timespec ts{ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

struct PfProc {
    pid_t pid;
    int in_w, out_r;
    bool exited;
    int exit_code;
    bool group;
};

static bool executable_file(const char *path) { return pf_is_file(path) && access(path, X_OK) == 0; }

char *pf_executable(void) {
    char stack[4096];
    uint32_t size = sizeof stack;
    char *buf = stack;
    if (_NSGetExecutablePath(buf, &size) != 0) {
        buf = static_cast<char *>(na_alloc(size));
        if (_NSGetExecutablePath(buf, &size) != 0) {
            free(buf);
            return NULL;
        }
    }
    char *resolved = realpath(buf, nullptr);
    if (buf != stack) free(buf);
    if (!resolved) return NULL;
    char *owned = na_strdup(resolved);
    free(resolved);
    return owned;
}

char *pf_which(const char *exe) {
    if (strchr(exe, '/')) return executable_file(exe) ? pf_fullpath(exe) : NULL;
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";
    const char *cursor = path;
    while (true) {
        const char *end = strchr(cursor, ':');
        size_t n = end ? (size_t)(end - cursor) : strlen(cursor);
        Buf candidate;
        buf_init(&candidate);
        if (n == 0) buf_puts(&candidate, ".");
        else buf_put(&candidate, cursor, n);
        buf_printf(&candidate, "/%s", exe);
        bool ok = executable_file(candidate.p);
        char *full = ok ? pf_fullpath(candidate.p) : NULL;
        buf_free(&candidate);
        if (full) return full;
        if (!end) return NULL;
        cursor = end + 1;
    }
}

static char **split_args(const char *full, const char *args, size_t *count) {
    size_t cap = 8, n = 0;
    char **v = static_cast<char **>(na_alloc(cap * sizeof *v));
    v[n++] = na_strdup(full);
    const char *p = args ? args : "";
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        Buf word;
        buf_init(&word);
        bool quoted = false;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            if (*p == '"') quoted = !quoted;
            else buf_putc(&word, *p);
            p++;
        }
        if (n + 2 > cap) {
            cap *= 2;
            v = static_cast<char **>(na_realloc(v, cap * sizeof *v));
        }
        v[n++] = buf_take(&word);
    }
    v[n] = NULL;
    *count = n;
    return v;
}

static PfProc *spawn_impl(const char *exe, const char *args, const char *cwd, bool capture) {
    char *full = pf_which(exe);
    if (!full) {
        na_seterr("executable not found: %s", exe);
        return NULL;
    }
    int input[2] = {-1, -1}, output[2] = {-1, -1};
    if (pipe(input) != 0 || pipe(output) != 0) {
        for (int fd : {input[0], input[1], output[0], output[1]})
            if (fd >= 0) close(fd);
        free(full);
        na_seterr("cannot create pipes");
        return NULL;
    }
    for (int fd : {input[0], input[1], output[0], output[1]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
    static const bool sigpipe_ignored = []() {
        signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)sigpipe_ignored;
    size_t argc;
    char **argv = split_args(full, args, &argc);
    pid_t pid = fork();
    if (pid == 0) {
        if (capture) setpgid(0, 0);
        dup2(input[0], STDIN_FILENO);
        dup2(output[1], STDOUT_FILENO);
        if (capture) dup2(output[1], STDERR_FILENO);
        else {
            int nul = open("/dev/null", O_WRONLY | O_CLOEXEC);
            if (nul >= 0) dup2(nul, STDERR_FILENO);
        }
        if (cwd) chdir(cwd);
        execve(full, argv, environ);
        _exit(127);
    }
    for (size_t i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    free(full);
    close(input[0]);
    close(output[1]);
    if (pid < 0) {
        close(input[1]);
        close(output[0]);
        na_seterr("cannot start %s", exe);
        return NULL;
    }
    if (capture) setpgid(pid, pid);
    PfProc *process = static_cast<PfProc *>(na_alloc(sizeof *process));
    process->pid = pid;
    process->group = capture;
    process->exit_code = -1;
    process->in_w = input[1];
    process->out_r = output[0];
    return process;
}

PfProc *pf_spawn(const char *exe, const char *args, const char *cwd) { return spawn_impl(exe, args, cwd, false); }
PfProc *pf_spawn_capture(const char *exe, const char *args, const char *cwd) { return spawn_impl(exe, args, cwd, true); }
int pf_proc_exit_code(PfProc *process) { return process ? process->exit_code : -1; }

int pf_proc_write(PfProc *process, const void *data, size_t n) {
    if (!process || process->in_w < 0) return -1;
    return write_all(process->in_w, data, n) == 0 ? 0 : -1;
}

bool pf_proc_alive(PfProc *process) {
    if (!process || process->exited) return false;
    int status = 0;
    pid_t result = waitpid(process->pid, &status, WNOHANG);
    if (result == process->pid || (result < 0 && errno != EINTR)) {
        if (result == process->pid)
            process->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        process->exited = true;
        return false;
    }
    return true;
}

int pf_proc_read(PfProc *process, void *buf, size_t cap, int timeout_ms) {
    if (!process || process->out_r < 0) return -1;
    pollfd pfd{(int)process->out_r, POLLIN, 0};
    int ready = poll(&pfd, 1, timeout_ms < 0 ? 0 : timeout_ms);
    if (ready < 0 && errno == EINTR) return 0;
    if (ready < 0) return -1;
    if (ready == 0) return 0;
    ssize_t n = read(process->out_r, buf, cap);
    if (interrupted(n)) return 0;
    if (n <= 0) return -1;
    return (int)n;
}

void pf_proc_close(PfProc *process, int graceful_ms) {
    if (!process) return;
    if (process->group) kill(-process->pid, SIGKILL);
    if (process->in_w >= 0) close(process->in_w);
    process->in_w = -1;
    if (!process->exited) {
        uint64_t deadline = pf_now_ms() + (uint64_t)(graceful_ms < 0 ? 0 : graceful_ms);
        while (pf_proc_alive(process) && pf_now_ms() < deadline) pf_sleep_ms(2);
        if (!process->exited) {
            kill(process->pid, SIGKILL);
            int status = 0;
            waitpid(process->pid, &status, 0);
        }
    }
    if (process->out_r >= 0) close(process->out_r);
    free(process);
}

struct PfSock {
    int fd;
};

static bool parse_ipv4(const char *host, uint8_t out[4]) {
    if (strcmp(host, "localhost") == 0) host = "127.0.0.1";
    const char *p = host;
    for (int i = 0; i < 4; i++) {
        if (*p < '0' || *p > '9') return false;
        unsigned value = 0;
        while (*p >= '0' && *p <= '9') value = value * 10 + (unsigned)(*p++ - '0');
        if (value > 255 || (i < 3 && *p++ != '.')) return false;
        out[i] = (uint8_t)value;
    }
    return *p == 0;
}

PfSock *pf_tcp_connect(const char *host, int port, int timeout_ms) {
    uint8_t ip[4];
    if (!parse_ipv4(host, ip)) {
        na_seterr("embedding url: host must be an IPv4 address or localhost (got %s)", host);
        return NULL;
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        na_seterr("cannot create socket");
        return NULL;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int nosig = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    memcpy(&address.sin_addr, ip, 4);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
        close(fd);
        na_seterr("cannot connect to %s:%d", host, port);
        return NULL;
    }
    PfSock *socket_handle = static_cast<PfSock *>(na_alloc(sizeof *socket_handle));
    socket_handle->fd = fd;
    return socket_handle;
}

int pf_sock_write(PfSock *socket_handle, const void *data, size_t n) {
    return write_all(socket_handle->fd, data, n);
}

int pf_sock_read(PfSock *socket_handle, void *buf, size_t cap, int timeout_ms) {
    pollfd pfd{socket_handle->fd, POLLIN, 0};
    int ready = poll(&pfd, 1, timeout_ms < 0 ? 0 : timeout_ms);
    if (ready < 0 && errno == EINTR) return 0;
    if (ready < 0) return -1;
    if (ready == 0) return 0;
    ssize_t n = recv(socket_handle->fd, buf, cap, 0);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
    return n <= 0 ? -1 : (int)n;
}

void pf_sock_close(PfSock *socket_handle) {
    if (!socket_handle) return;
    close(socket_handle->fd);
    free(socket_handle);
}

bool pf_stat(const char *path, PfStat *out) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    out->size = (uint64_t)st.st_size;
    out->mtime_ms = (int64_t)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
    return true;
}

int64_t pf_unix_ms(void) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
