/* PAL backend for Windows (x86_64 / aarch64), talking to kernel32 directly.
 * Built without the Windows SDK: see win.h and the *.def files. */
#include "../pal.h"
#include "win.h"
#include "counter_math.h"
#include <errno.h>
#include <limits.h>

const char pal_name[] =
#if defined(__x86_64__)
    "windows-x86_64";
#elif defined(__aarch64__)
    "windows-aarch64";
#else
    "windows";
#endif
const char pal_path_sep = '\\';
char **pal_environ;

/* ---------------------------------------------------------------------- */
/* error translation                                                       */
/* ---------------------------------------------------------------------- */

static int map_error(DWORD e) {
    switch (e) {
    case 0: return 0;
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE:
    case ERROR_INVALID_NAME: return ENOENT;
    case ERROR_TOO_MANY_OPEN_FILES: return EMFILE;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION:
    case ERROR_CANT_ACCESS_FILE: return EACCES;
    case ERROR_WRITE_PROTECT: return EROFS;
    case ERROR_INVALID_HANDLE: return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_NOT_ENOUGH_QUOTA: return ENOMEM;
    case ERROR_NOT_SAME_DEVICE: return EXDEV;
    case ERROR_CURRENT_DIRECTORY: return EBUSY;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return EEXIST;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    case ERROR_DIR_NOT_EMPTY: return ENOTEMPTY;
    case ERROR_BROKEN_PIPE: case ERROR_NO_DATA: case ERROR_PIPE_NOT_CONNECTED: return EPIPE;
    case ERROR_INVALID_PARAMETER: case ERROR_NEGATIVE_SEEK: return EINVAL;
    case ERROR_SEEK_ON_DEVICE: return ESPIPE;
    case ERROR_NOT_SUPPORTED: case ERROR_CALL_NOT_IMPLEMENTED: return ENOSYS;
    case ERROR_FILENAME_EXCED_RANGE: return ENAMETOOLONG;
    case ERROR_INSUFFICIENT_BUFFER: return ERANGE;
    case ERROR_TIMEOUT: return ETIMEDOUT;
    case ERROR_OPERATION_ABORTED: return EINTR;
    default: return EIO;
    }
}
static int last_error(void) { int e = map_error(GetLastError()); return -(e ? e : EIO); }

/* ---------------------------------------------------------------------- */
/* UTF-8 <-> UTF-16 helpers                                                */
/* ---------------------------------------------------------------------- */

#define PATH_MAX_W 32768

/* Convert UTF-8 to a temporary UTF-16 buffer on the stack-allocated storage
 * `buf` (capacity cap, in code units).  Returns 0 or -errno. */
static int to_wide(const char *s, WCHAR *buf, int cap) {
    if (!s) return -EINVAL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, cap);
    if (n <= 0) return GetLastError() == ERROR_INSUFFICIENT_BUFFER ? -ENAMETOOLONG : -EINVAL;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Process                                                                 */
/* ---------------------------------------------------------------------- */

_Noreturn void pal_exit(int status) {
    ExitProcess((unsigned)status);
    for (;;) {}
}
_Noreturn void pal_abort(void) {
    TerminateProcess(GetCurrentProcess(), 3);
    ExitProcess(3);
    for (;;) {}
}

int pal_random(void *buf, size_t n) {
    unsigned char *p = buf;
    while (n) {
        size_t c = n > 0x10000000u ? 0x10000000u : n;
        if (!ProcessPrng(p, c)) return -EIO;
        p += c; n -= c;
    }
    return 0;
}

int pal_system(const char *command, int *status) {
    static const WCHAR kComSpec[] = u"ComSpec";
    static const WCHAR kDefault[] = u"cmd.exe";
    WCHAR comspec[520];
    DWORD len = GetEnvironmentVariableW(kComSpec, comspec, 520);
    if (len == 0 || len >= 520) {
        for (int i = 0; i < 8; i++) comspec[i] = kDefault[i];
    }
    if (!command) {
        int ignored;
        *status = pal_system("exit 0", &ignored) == 0;
        return 0;
    }

    /* command line: <comspec> /d /s /c "<command>" */
    static const WCHAR kOpts[] = u" /d /s /c \"";
    int cl = 0; while (comspec[cl]) cl++;
    int total_cap = cl + 16 + (int)(2 * 4 * 1) + 1;
    /* conservative: converted command length <= strlen*1 UTF-16 units */
    size_t clen = 0; while (command[clen]) clen++;
    if (clen > 32000) return -E2BIG;
    WCHAR *line = pal_mem_map(((size_t)(total_cap) + clen + 4096) * sizeof(WCHAR) / 4096 * 4096 + 4096);
    if (!line) return -ENOMEM;
    int pos = 0;
    line[pos++] = u'"';
    for (int i = 0; i < cl; i++) line[pos++] = comspec[i];
    line[pos++] = u'"';
    for (int i = 0; kOpts[i]; i++) line[pos++] = kOpts[i];
    if (clen) {
        int n = MultiByteToWideChar(CP_UTF8, 0, command, -1, line + pos, (int)clen + 1);
        if (n <= 0) { pal_mem_unmap(line, ((size_t)(total_cap) + clen + 4096) * sizeof(WCHAR) / 4096 * 4096 + 4096); return -EINVAL; }
        pos += n - 1;
    }
    line[pos++] = u'"';
    line[pos] = 0;

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    for (size_t i = 0; i < sizeof si; i++) ((char *)&si)[i] = 0;
    si.cb = (DWORD)sizeof si;
    BOOL ok = CreateProcessW(NULL, line, NULL, NULL, 1, 0, NULL, NULL, &si, &pi);
    int err = ok ? 0 : last_error();
    pal_mem_unmap(line, ((size_t)(total_cap) + clen + 4096) * sizeof(WCHAR) / 4096 * 4096 + 4096);
    if (!ok) return err;
    DWORD code = 0;
    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0)
        err = last_error();
    else if (!GetExitCodeProcess(pi.hProcess, &code))
        err = last_error();
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (err) return err;
    *status = (int)code;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Memory                                                                  */
/* ---------------------------------------------------------------------- */

size_t pal_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}
void *pal_mem_map(size_t size) {
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
int pal_mem_unmap(void *ptr, size_t size) {
    (void)size;
    return VirtualFree(ptr, 0, MEM_RELEASE) ? 0 : last_error();
}

/* ---------------------------------------------------------------------- */
/* Files                                                                   */
/* ---------------------------------------------------------------------- */

int pal_open(const char *path, int flags, pal_file_t *out) {
    WCHAR wpath[PATH_MAX_W > 4096 ? 4096 : PATH_MAX_W];
    int rc = to_wide(path, wpath, (int)(sizeof wpath / sizeof wpath[0]));
    if (rc) return rc;

    DWORD access = 0;
    if (flags & PAL_O_RDONLY) access |= GENERIC_READ;
    if (flags & PAL_O_WRONLY) access |= (flags & PAL_O_APPEND) ? FILE_APPEND_DATA : GENERIC_WRITE;
    if (!(flags & PAL_O_RDWR)) return -EINVAL;
    if ((flags & PAL_O_TRUNC) && (flags & PAL_O_APPEND)) access |= GENERIC_WRITE;

    DWORD disp;
    if (flags & PAL_O_CREAT) {
        if (flags & PAL_O_EXCL) disp = CREATE_NEW;
        else if (flags & PAL_O_TRUNC) disp = CREATE_ALWAYS;
        else disp = OPEN_ALWAYS;
    } else if (flags & PAL_O_TRUNC) disp = TRUNCATE_EXISTING;
    else disp = OPEN_EXISTING;

    DWORD attrs = FILE_ATTRIBUTE_NORMAL;
    if (flags & PAL_O_TEMP) attrs |= FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE h = CreateFileW(wpath, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, disp, attrs, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        int e = last_error();
        /* Opening a directory for reading is not an error at the Win32 level
         * with CreateFile, but ACCESS_DENIED is reported: map to EISDIR. */
        if (e == -EACCES) {
            DWORD a = GetFileAttributesW(wpath);
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return -EISDIR;
        }
        return e;
    }
    *out = (pal_file_t)h;
    return 0;
}

int pal_close(pal_file_t f) {
    return CloseHandle((HANDLE)f) ? 0 : last_error();
}

int pal_read(pal_file_t f, void *buf, size_t n, size_t *got) {
    if (f == PAL_INVALID_FILE || f == 0) return -EBADF;
    DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n, r = 0;
    if (!ReadFile((HANDLE)f, buf, want, &r, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) { *got = 0; return 0; }
        return -map_error(e);
    }
    *got = r;
    return 0;
}

int pal_write(pal_file_t f, const void *buf, size_t n, size_t *written) {
    if (f == PAL_INVALID_FILE || f == 0) return -EBADF;
    DWORD want = n > 0x40000000u ? 0x40000000u : (DWORD)n, w = 0;
    if (!WriteFile((HANDLE)f, buf, want, &w, NULL)) return last_error();
    *written = w;
    return 0;
}

int pal_seek(pal_file_t f, int64_t off, int whence, int64_t *newpos) {
    if (f == PAL_INVALID_FILE || f == 0) return -EBADF;
    if (whence != PAL_SEEK_SET && whence != PAL_SEEK_CUR && whence != PAL_SEEK_END) return -EINVAL;
    SetLastError(0);
    DWORD type = GetFileType((HANDLE)f);
    if (type == FILE_TYPE_UNKNOWN && GetLastError() != 0) return last_error();
    if (type != FILE_TYPE_DISK) return -ESPIPE;
    LARGE_INTEGER li, res;
    li.QuadPart = off;
    DWORD method = whence == PAL_SEEK_SET ? 0 : whence == PAL_SEEK_CUR ? 1 : 2;
    if (!SetFilePointerEx((HANDLE)f, li, &res, method)) return last_error();
    if (newpos) *newpos = res.QuadPart;
    return 0;
}

int pal_sync(pal_file_t f) {
    if (f == PAL_INVALID_FILE || f == 0) return -EBADF;
    return FlushFileBuffers((HANDLE)f) ? 0 : last_error();
}

int pal_isatty(pal_file_t f) {
    DWORD m;
    if (f == PAL_INVALID_FILE || f == 0) return 0;
    return GetConsoleMode((HANDLE)f, &m) ? 1 : 0;
}

pal_file_t pal_std_file(int n) {
    if (n < 0 || n > 2) return PAL_INVALID_FILE;
    HANDLE h = GetStdHandle(n == 0 ? STD_INPUT_HANDLE : n == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    if (!h) return PAL_INVALID_FILE;
    return (pal_file_t)h;
}

int pal_remove(const char *path) {
    WCHAR w[4096];
    int rc = to_wide(path, w, 4096);
    if (rc) return rc;
    DWORD a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return last_error();
    BOOL ok = (a & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(w) : DeleteFileW(w);
    return ok ? 0 : last_error();
}

int pal_rename(const char *from, const char *to) {
    WCHAR a[4096], b[4096];
    int rc = to_wide(from, a, 4096);
    if (rc) return rc;
    rc = to_wide(to, b, 4096);
    if (rc) return rc;
    return MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : last_error();
}

int pal_tmpdir(char *buf, size_t n) {
    WCHAR w[1024];
    DWORD len = GetTempPathW(1024, w);
    if (len == 0) return last_error();
    if (len >= 1024) return -ERANGE;
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return -EINVAL;
    if ((size_t)need > n) return -ERANGE;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, need, NULL, NULL)) return -EINVAL;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Time                                                                    */
/* ---------------------------------------------------------------------- */

static uint64_t ft_u64(FILETIME ft) {
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
static void ticks100_to_ts(uint64_t t, struct pal_timespec *ts) {
    ts->sec = (int64_t)(t / 10000000u);
    ts->nsec = (int32_t)((t % 10000000u) * 100u);
}

int pal_clock_gettime(int clock, struct pal_timespec *ts) {
    switch (clock) {
    case PAL_CLOCK_REALTIME: {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        uint64_t t = ft_u64(ft);
        const uint64_t epoch = 116444736000000000ull;
        if (t >= epoch) ticks100_to_ts(t - epoch, ts);
        else {
            uint64_t before = epoch - t;
            ts->sec = -(int64_t)(before / 10000000u);
            ts->nsec = (int32_t)((before % 10000000u) * 100u);
            if (ts->nsec) { --ts->sec; ts->nsec = 1000000000 - ts->nsec; }
        }
        return 0;
    }
    case PAL_CLOCK_MONOTONIC: {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return -EIO;
        int64_t freq = f.QuadPart;
        LARGE_INTEGER c;
        if (!QueryPerformanceCounter(&c) || c.QuadPart < 0) return -EIO;
        ts->sec = c.QuadPart / freq;
        ts->nsec = counter_nanoseconds((uint64_t)(c.QuadPart % freq), (uint64_t)freq);
        return 0;
    }
    case PAL_CLOCK_PROCESS_CPU: case PAL_CLOCK_THREAD_CPU: {
        FILETIME cr, ex, kr, us;
        BOOL ok = clock == PAL_CLOCK_PROCESS_CPU ? GetProcessTimes(GetCurrentProcess(), &cr, &ex, &kr, &us)
                                                 : GetThreadTimes(GetCurrentThread(), &cr, &ex, &kr, &us);
        if (!ok) return last_error();
        ticks100_to_ts(ft_u64(kr) + ft_u64(us), ts);
        return 0;
    }
    default: return -EINVAL;
    }
}

int pal_clock_getres(int clock, struct pal_timespec *ts) {
    ts->sec = 0;
    switch (clock) {
    case PAL_CLOCK_REALTIME: case PAL_CLOCK_PROCESS_CPU: case PAL_CLOCK_THREAD_CPU:
        ts->nsec = 100; return 0;
    case PAL_CLOCK_MONOTONIC: {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) return -EIO;
        int64_t r = 1000000000ll / f.QuadPart;
        ts->nsec = (int32_t)(r ? r : 1);
        return 0;
    }
    default: return -EINVAL;
    }
}

int pal_sleep(const struct pal_timespec *d) {
    if (d->sec < 0 || d->nsec < 0 || d->nsec > 999999999) return -EINVAL;
    uint64_t seconds = (uint64_t)d->sec;
    while (seconds >= 4294967u) { Sleep(4294967000u); seconds -= 4294967u; }
    uint64_t ms = seconds * 1000u + (uint64_t)(d->nsec + 999999) / 1000000u;
    while (ms > 0xFFFFFFFEu) { Sleep(0xFFFFFFFEu); ms -= 0xFFFFFFFEu; }
    Sleep((DWORD)ms);
    return 0;
}

/* civil date helpers (proleptic Gregorian), days relative to 1970-01-01 */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static int64_t year_of_days(int64_t z) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned m = mp + (mp < 10 ? 3 : -9);
    return y + (m <= 2);
}
static int days_in_month(int64_t y, int m) {
    static const unsigned char dm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

/* seconds since epoch of the (local) wall-clock instant described by a
 * TIME_ZONE_INFORMATION transition rule in the given year */
static int64_t rule_time(int64_t year, const SYSTEMTIME *st) {
    int64_t day;
    if (st->wYear != 0) {
        day = days_from_civil(st->wYear, st->wMonth, st->wDay);
    } else {
        int64_t first = days_from_civil(year, st->wMonth, 1);
        int wd_first = (int)(((first % 7) + 11) % 7); /* 1970-01-01 was Thursday(4) */
        int delta = (st->wDayOfWeek - wd_first + 7) % 7;
        int d = 1 + delta + 7 * (st->wDay - 1);
        int dim = days_in_month(year, st->wMonth);
        while (d > dim) d -= 7;
        day = first + (d - 1);
    }
    return day * 86400 + st->wHour * 3600 + st->wMinute * 60 + st->wSecond;
}

static void format_abbr(char out[16], int32_t off) {
    if (off == 0) { out[0] = 'U'; out[1] = 'T'; out[2] = 'C'; out[3] = 0; return; }
    int i = 0;
    out[i++] = off < 0 ? '-' : '+';
    int32_t a = off < 0 ? -off : off;
    int hh = a / 3600, mm = (a % 3600) / 60;
    out[i++] = (char)('0' + hh / 10);
    out[i++] = (char)('0' + hh % 10);
    if (mm) { out[i++] = (char)('0' + mm / 10); out[i++] = (char)('0' + mm % 10); }
    out[i] = 0;
}

int pal_tz_lookup(int64_t utc, struct pal_tzinfo *out) {
    int64_t days = utc / 86400;
    if (utc % 86400 < 0) --days;
    int64_t year = year_of_days(days);
    if (year < 1601) year = 1601;
    if (year > 30827) year = 30827;
    TIME_ZONE_INFORMATION tz;
    if (!GetTimeZoneInformationForYear((WORD)year, NULL, &tz)) {
        out->utc_offset = 0; out->is_dst = 0;
        format_abbr(out->abbr, 0);
        return 0;
    }
    int32_t std_off = -(tz.Bias + tz.StandardBias) * 60;
    int32_t dst_off = -(tz.Bias + tz.DaylightBias) * 60;
    int dst = 0;
    if (tz.StandardDate.wMonth != 0 && tz.DaylightDate.wMonth != 0) {
        int64_t start = rule_time(year, &tz.DaylightDate) - std_off; /* local std -> UTC */
        int64_t end   = rule_time(year, &tz.StandardDate) - dst_off; /* local dst -> UTC */
        if (start < end) dst = (utc >= start && utc < end);
        else             dst = (utc >= start || utc < end);
    }
    out->utc_offset = dst ? dst_off : std_off;
    out->is_dst = dst;
    format_abbr(out->abbr, out->utc_offset);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Threads and synchronisation                                             */
/* ---------------------------------------------------------------------- */

struct thread_start { void (*fn)(void *); void *arg; };

static DWORD thread_trampoline(LPVOID p) {
    struct thread_start ts = *(struct thread_start *)p;
    HeapFree(GetProcessHeap(), 0, p);
    ts.fn(ts.arg);
    return 0;
}

int pal_thread_create(void (*fn)(void *), void *arg, size_t stack_size, pal_thread_t *out) {
    struct thread_start *ts = HeapAlloc(GetProcessHeap(), 0, sizeof *ts);
    if (!ts) return -ENOMEM;
    ts->fn = fn; ts->arg = arg;
    HANDLE h = CreateThread(NULL, stack_size, thread_trampoline, ts,
                            stack_size ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, NULL);
    if (!h) { int e = last_error(); HeapFree(GetProcessHeap(), 0, ts); return e == 0 ? -EAGAIN : e; }
    *out = (pal_thread_t)h;
    return 0;
}
int pal_thread_join(pal_thread_t t) {
    if (WaitForSingleObject((HANDLE)t, INFINITE) != WAIT_OBJECT_0) return last_error();
    return CloseHandle((HANDLE)t) ? 0 : last_error();
}
int pal_thread_detach(pal_thread_t t) {
    return CloseHandle((HANDLE)t) ? 0 : last_error();
}
_Noreturn void pal_thread_exit(void) {
    ExitThread(0);
}
pal_tid_t pal_thread_self(void) {
    return (pal_tid_t)GetCurrentThreadId();
}
void pal_thread_yield(void) {
    if (!SwitchToThread()) Sleep(0);
}

int pal_futex_wait(volatile int32_t *addr, int32_t expected, int64_t timeout_ns) {
    DWORD ms;
    if (timeout_ns < 0) ms = INFINITE;
    else {
        int64_t m = timeout_ns / 1000000 + (timeout_ns % 1000000 != 0);
        ms = m > 0xFFFFFFFEll ? 0xFFFFFFFEu : (DWORD)m;
    }
    int32_t cmp = expected;
    if (!WaitOnAddress(addr, &cmp, sizeof cmp, ms)) {
        if (GetLastError() == ERROR_TIMEOUT) return -ETIMEDOUT;
    }
    return 0;
}
void pal_futex_wake(volatile int32_t *addr, int32_t count) {
    if (count <= 0) return;
    if (count == 1) WakeByAddressSingle((void *)addr);
    else WakeByAddressAll((void *)addr);
}

/* ---------------------------------------------------------------------- */
/* Signals                                                                 */
/* ---------------------------------------------------------------------- */

#define LSIGINT  2
#define LSIGILL  4
#define LSIGFPE  8
#define LSIGSEGV 11
#define LSIGTERM 15

static int sig_enabled[32];
static LPVOID veh_handle;
static int ctrl_installed;
static int signal_lock;

static long veh(EXCEPTION_POINTERS *ep) {
    int sig = 0;
    switch (ep->ExceptionRecord->ExceptionCode) {
    case 0xC0000005u: case 0xC0000006u: sig = LSIGSEGV; break;
    case 0xC000001Du: case 0xC0000096u: case 0xC000001Eu: sig = LSIGILL; break;
    case 0xC000008Eu: case 0xC000008Fu: case 0xC0000090u: case 0xC0000091u:
    case 0xC0000092u: case 0xC0000093u: case 0xC0000094u: case 0xC0000095u: sig = LSIGFPE; break;
    default: return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!__atomic_load_n(&sig_enabled[sig], __ATOMIC_ACQUIRE)) return EXCEPTION_CONTINUE_SEARCH;
    __libc_signal_dispatch(sig);
    pal_abort(); /* handler returned from a fault signal: undefined behaviour */
}

static BOOL ctrl_handler(DWORD type) {
    int sig;
    switch (type) {
    case 0: case 1: sig = LSIGINT; break;       /* CTRL_C / CTRL_BREAK */
    case 2: case 5: case 6: sig = LSIGTERM; break; /* close / logoff / shutdown */
    default: return 0;
    }
    if (!__atomic_load_n(&sig_enabled[sig], __ATOMIC_ACQUIRE)) return 0;
    __libc_signal_dispatch(sig);
    return 1;
}

int pal_signal_enable(int sig, int on) {
    int rc = 0;
    while (__atomic_exchange_n(&signal_lock, 1, __ATOMIC_ACQUIRE)) SwitchToThread();
    switch (sig) {
    case LSIGSEGV: case LSIGILL: case LSIGFPE:
        if (on && !veh_handle) {
            veh_handle = AddVectoredExceptionHandler(1, veh);
            if (!veh_handle) { rc = -EINVAL; break; }
        }
        __atomic_store_n(&sig_enabled[sig], !!on, __ATOMIC_RELEASE);
        break;
    case LSIGINT: case LSIGTERM:
        if (on && !ctrl_installed) {
            if (!SetConsoleCtrlHandler(ctrl_handler, 1)) { rc = -EINVAL; break; }
            ctrl_installed = 1;
        }
        __atomic_store_n(&sig_enabled[sig], !!on, __ATOMIC_RELEASE);
        break;
    default:
        rc = -EINVAL;
        break;
    }
    __atomic_store_n(&signal_lock, 0, __ATOMIC_RELEASE);
    return rc;
}
