#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal Win32 declarations (resolved from kernel32.dll at link time) ---- */
typedef void *HANDLE;
typedef uint32_t DWORD;
typedef int BOOL;
typedef uint16_t WCHAR;

typedef struct {
    DWORD nLength;
    void *lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct {
    DWORD cb;
    WCHAR *lpReserved, *lpDesktop, *lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    uint16_t wShowWindow, cbReserved2;
    void *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;

typedef struct {
    HANDLE hProcess, hThread;
    DWORD dwProcessId, dwThreadId;
} PROCESS_INFORMATION;

typedef struct {
    DWORD dwFileAttributes;
    DWORD ftCreationTime[2], ftLastAccessTime[2], ftLastWriteTime[2];
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef union {
    int64_t QuadPart;
} LARGE_INTEGER;

HANDLE CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
BOOL WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
BOOL CloseHandle(HANDLE);
BOOL FlushFileBuffers(HANDLE);
BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER *, DWORD);
BOOL SetEndOfFile(HANDLE);
BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *);
DWORD GetFileAttributesW(const WCHAR *);
BOOL CreateDirectoryW(const WCHAR *, void *);
BOOL DeleteFileW(const WCHAR *);
BOOL MoveFileExW(const WCHAR *, const WCHAR *, DWORD);
DWORD GetFullPathNameW(const WCHAR *, DWORD, WCHAR *, WCHAR **);
DWORD GetLastError(void);
HANDLE FindFirstFileW(const WCHAR *, WIN32_FIND_DATAW *);
BOOL FindNextFileW(HANDLE, WIN32_FIND_DATAW *);
BOOL FindClose(HANDLE);
uint64_t GetTickCount64(void);
void Sleep(DWORD);
DWORD GetCurrentProcessId(void);
BOOL CreatePipe(HANDLE *, HANDLE *, SECURITY_ATTRIBUTES *, DWORD);
BOOL SetHandleInformation(HANDLE, DWORD, DWORD);
BOOL PeekNamedPipe(HANDLE, void *, DWORD, DWORD *, DWORD *, DWORD *);
BOOL CreateProcessW(const WCHAR *, WCHAR *, void *, void *, BOOL, DWORD, void *, const WCHAR *,
                    STARTUPINFOW *, PROCESS_INFORMATION *);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL TerminateProcess(HANDLE, unsigned);
BOOL GetExitCodeProcess(HANDLE, DWORD *);
DWORD SearchPathW(const WCHAR *, const WCHAR *, const WCHAR *, DWORD, WCHAR *, WCHAR **);
int MultiByteToWideChar(unsigned, DWORD, const char *, int, WCHAR *, int);
int WideCharToMultiByte(unsigned, DWORD, const WCHAR *, int, char *, int, const char *, BOOL *);

#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_ALL 7u
#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define ATTR_DIRECTORY 0x10u
#define ATTR_REPARSE 0x400u
#define INVALID_ATTRS 0xFFFFFFFFu
#define DELETE_ON_CLOSE 0x04000000u
#define ERR_FILE_NOT_FOUND 2u
#define ERR_PATH_NOT_FOUND 3u
#define ERR_ALREADY_EXISTS 183u
#define CP_UTF8_ 65001u

static WCHAR *to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8_, 0, s, -1, NULL, 0);
    WCHAR *w = na_alloc((size_t)(n > 0 ? n : 1) * sizeof(WCHAR));
    if (n > 0) MultiByteToWideChar(CP_UTF8_, 0, s, -1, w, n);
    return w;
}

static char *to_narrow(const WCHAR *w) {
    int n = WideCharToMultiByte(CP_UTF8_, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = na_alloc((size_t)(n > 0 ? n : 1));
    if (n > 0) WideCharToMultiByte(CP_UTF8_, 0, w, -1, s, n, NULL, NULL);
    return s;
}

static DWORD attrs(const char *path) {
    WCHAR *w = to_wide(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a;
}

bool pf_is_file(const char *path) {
    DWORD a = attrs(path);
    return a != INVALID_ATTRS && !(a & ATTR_DIRECTORY);
}

bool pf_is_dir(const char *path) {
    DWORD a = attrs(path);
    return a != INVALID_ATTRS && (a & ATTR_DIRECTORY);
}

bool pf_is_reparse(const char *path) {
    DWORD a = attrs(path);
    return a != INVALID_ATTRS && (a & ATTR_REPARSE);
}

static int read_handle(HANDLE h, uint64_t offset, Buf *out) {
    if (offset) {
        LARGE_INTEGER li;
        li.QuadPart = (int64_t)offset;
        if (!SetFilePointerEx(h, li, NULL, 0)) return -1;
    }
    char chunk[65536];
    while (true) {
        DWORD got = 0;
        if (!ReadFile(h, chunk, sizeof chunk, &got, NULL)) return -1;
        if (got == 0) break;
        buf_put(out, chunk, got);
    }
    return 0;
}

int pf_read_from(const char *path, uint64_t offset, Buf *out) {
    WCHAR *w = to_wide(path);
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_ALL, NULL, OPEN_EXISTING, 0, NULL);
    free(w);
    if (h == INVALID_HANDLE) {
        DWORD e = GetLastError();
        if (e == ERR_FILE_NOT_FOUND || e == ERR_PATH_NOT_FOUND) return 1;
        return na_fail("cannot open %s (error %u)", path, (unsigned)e);
    }
    int rc = read_handle(h, offset, out);
    CloseHandle(h);
    if (rc != 0) return na_fail("cannot read %s", path);
    return 0;
}

int pf_read_file(const char *path, Buf *out) { return pf_read_from(path, 0, out); }

int pf_mkdirs(const char *dir) {
    if (pf_is_dir(dir)) return 0;
    char *copy = na_strdup(dir);
    for (size_t i = 1; copy[i]; i++) {
        if (copy[i] == '/' && copy[i - 1] != ':') {
            copy[i] = 0;
            if (!pf_is_dir(copy)) {
                WCHAR *w = to_wide(copy);
                BOOL ok = CreateDirectoryW(w, NULL);
                free(w);
                if (!ok && GetLastError() != ERR_ALREADY_EXISTS) {
                    int rc = na_fail("cannot create directory %s", copy);
                    free(copy);
                    return rc;
                }
            }
            copy[i] = '/';
        }
    }
    WCHAR *w = to_wide(copy);
    BOOL ok = CreateDirectoryW(w, NULL);
    free(w);
    free(copy);
    if (!ok && GetLastError() != ERR_ALREADY_EXISTS) return na_fail("cannot create directory %s", dir);
    return 0;
}

static int write_all(HANDLE h, const void *data, size_t n) {
    const char *p = data;
    while (n > 0) {
        DWORD chunk = n > 0x40000000u ? 0x40000000u : (DWORD)n;
        DWORD done = 0;
        if (!WriteFile(h, p, chunk, &done, NULL) || done == 0) return -1;
        p += done;
        n -= done;
    }
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
    buf_printf(&tmp, ".aed-%u-%llu-%u.tmp", (unsigned)GetCurrentProcessId(),
               (unsigned long long)GetTickCount64(), counter++);
    WCHAR *wt = to_wide(tmp.p);
    HANDLE h = CreateFileW(wt, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (h == INVALID_HANDLE) {
        free(wt);
        buf_free(&tmp);
        return na_fail("cannot create temporary file next to %s", path);
    }
    int rc = write_all(h, data, n);
    if (rc == 0 && !FlushFileBuffers(h)) rc = -1;
    CloseHandle(h);
    if (rc != 0) {
        DeleteFileW(wt);
        free(wt);
        buf_free(&tmp);
        return na_fail("cannot write temporary file for %s", path);
    }
    WCHAR *wd = to_wide(path);
    BOOL ok = MoveFileExW(wt, wd, 1u | 8u); /* REPLACE_EXISTING | WRITE_THROUGH */
    if (!ok) {
        DWORD e = GetLastError();
        DeleteFileW(wt);
        free(wt);
        free(wd);
        buf_free(&tmp);
        return na_fail("cannot replace %s (error %u)", path, (unsigned)e);
    }
    free(wt);
    free(wd);
    buf_free(&tmp);
    return 0;
}

int pf_append_at(const char *path, uint64_t at, const void *data, size_t n) {
    WCHAR *w = to_wide(path);
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_ALL, NULL, OPEN_ALWAYS, 0, NULL);
    free(w);
    if (h == INVALID_HANDLE) return na_fail("cannot open journal %s", path);
    LARGE_INTEGER li;
    li.QuadPart = (int64_t)at;
    int rc = 0;
    if (!SetFilePointerEx(h, li, NULL, 0) || !SetEndOfFile(h)) rc = -1;
    if (rc == 0) rc = write_all(h, data, n);
    if (rc == 0 && !FlushFileBuffers(h)) rc = -1;
    CloseHandle(h);
    if (rc != 0) return na_fail("cannot append to journal %s", path);
    return 0;
}

int pf_remove(const char *path) {
    WCHAR *w = to_wide(path);
    BOOL ok = DeleteFileW(w);
    free(w);
    return ok ? 0 : -1;
}

char *pf_fullpath(const char *path) {
    WCHAR *w = to_wide(path);
    DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
    if (n == 0) {
        free(w);
        return NULL;
    }
    WCHAR *out = na_alloc(((size_t)n + 1) * sizeof(WCHAR));
    GetFullPathNameW(w, n + 1, out, NULL);
    free(w);
    char *narrow = to_narrow(out);
    free(out);
    char *norm = path_normalize(narrow);
    free(narrow);
    return norm;
}

int pf_list_dir(const char *dir, StrList *files, StrList *dirs) {
    Buf pat;
    buf_init(&pat);
    buf_puts(&pat, dir);
    buf_puts(&pat, "/*");
    WCHAR *w = to_wide(pat.p);
    buf_free(&pat);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE) return na_fail("cannot list %s", dir);
    do {
        char *name = to_narrow(fd.cFileName);
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && !(fd.dwFileAttributes & ATTR_REPARSE)) {
            if (fd.dwFileAttributes & ATTR_DIRECTORY) {
                if (dirs) sl_push(dirs, name);
            } else if (files) {
                sl_push(files, name);
            }
        }
        free(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

struct PfLock {
    HANDLE h;
};

PfLock *pf_lock_acquire(const char *path, int timeout_ms) {
    uint64_t deadline = GetTickCount64() + (uint64_t)timeout_ms;
    WCHAR *w = to_wide(path);
    while (true) {
        HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, DELETE_ON_CLOSE, NULL);
        if (h != INVALID_HANDLE) {
            free(w);
            PfLock *l = na_alloc(sizeof *l);
            l->h = h;
            return l;
        }
        if (GetTickCount64() >= deadline) break;
        Sleep(5);
    }
    free(w);
    na_seterr("store_locked: another process holds the workspace lock");
    return NULL;
}

void pf_lock_release(PfLock *lock) {
    if (!lock) return;
    CloseHandle(lock->h);
    free(lock);
}

uint64_t pf_now_ms(void) { return GetTickCount64(); }
void pf_sleep_ms(unsigned ms) { Sleep(ms); }

/* ---- child process ---- */
struct PfProc {
    HANDLE process, thread, in_w, out_r;
    bool exited;
};

char *pf_which(const char *exe) {
    WCHAR *w = to_wide(exe);
    bool has_sep = strchr(exe, '/') || strchr(exe, '\\');
    WCHAR buf[1024];
    DWORD n = 0;
    if (has_sep) {
        if (pf_is_file(exe)) {
            free(w);
            return pf_fullpath(exe);
        }
        static const WCHAR ext[] = {'.', 'e', 'x', 'e', 0};
        n = SearchPathW(NULL, w, ext, 1024, buf, NULL);
    } else {
        static const WCHAR ext[] = {'.', 'e', 'x', 'e', 0};
        n = SearchPathW(NULL, w, ext, 1024, buf, NULL);
    }
    free(w);
    if (n == 0 || n >= 1024) return NULL;
    char *narrow = to_narrow(buf);
    char *norm = path_normalize(narrow);
    free(narrow);
    return norm;
}

PfProc *pf_spawn(const char *exe, const char *args, const char *cwd) {
    char *full = pf_which(exe);
    if (!full) {
        na_seterr("executable not found: %s", exe);
        return NULL;
    }
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = 1;
    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL;
    if (!CreatePipe(&in_r, &in_w, &sa, 1 << 20) || !CreatePipe(&out_r, &out_w, &sa, 1 << 20)) {
        free(full);
        na_seterr("cannot create pipes");
        return NULL;
    }
    SetHandleInformation(in_w, 1, 0); /* parent ends are not inherited */
    SetHandleInformation(out_r, 1, 0);
    static const WCHAR nul[] = {'N', 'U', 'L', 0};
    HANDLE err_h = CreateFileW(nul, GENERIC_WRITE, FILE_SHARE_ALL, &sa, OPEN_EXISTING, 0, NULL);

    Buf cmd;
    buf_init(&cmd);
    buf_putc(&cmd, '"');
    for (const char *c = full; *c; c++) buf_putc(&cmd, *c == '/' ? '\\' : *c);
    buf_putc(&cmd, '"');
    if (args && *args) {
        buf_putc(&cmd, ' ');
        buf_puts(&cmd, args);
    }
    WCHAR *wcmd = to_wide(cmd.p);
    WCHAR *wcwd = cwd ? to_wide(cwd) : NULL;
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = 0x100; /* STARTF_USESTDHANDLES */
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = err_h == INVALID_HANDLE ? NULL : err_h;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, 1, 0x08000000u /* CREATE_NO_WINDOW */, NULL, wcwd,
                             &si, &pi);
    DWORD e = ok ? 0 : GetLastError();
    free(wcmd);
    free(wcwd);
    buf_free(&cmd);
    free(full);
    CloseHandle(in_r);
    CloseHandle(out_w);
    if (err_h != INVALID_HANDLE) CloseHandle(err_h);
    if (!ok) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        na_seterr("cannot start %s (error %u)", exe, (unsigned)e);
        return NULL;
    }
    PfProc *p = na_alloc(sizeof *p);
    p->process = pi.hProcess;
    p->thread = pi.hThread;
    p->in_w = in_w;
    p->out_r = out_r;
    return p;
}

int pf_proc_write(PfProc *p, const void *data, size_t n) {
    if (!p || !p->in_w) return -1;
    return write_all(p->in_w, data, n) == 0 ? 0 : -1;
}

bool pf_proc_alive(PfProc *p) {
    if (!p || p->exited) return false;
    if (WaitForSingleObject(p->process, 0) == 0) {
        p->exited = true;
        return false;
    }
    return true;
}

int pf_proc_read(PfProc *p, void *buf, size_t cap, int timeout_ms) {
    if (!p || !p->out_r) return -1;
    uint64_t deadline = GetTickCount64() + (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);
    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(p->out_r, NULL, 0, NULL, &avail, NULL)) return -1;
        if (avail > 0) {
            DWORD want = avail < cap ? avail : (DWORD)cap;
            DWORD got = 0;
            if (!ReadFile(p->out_r, buf, want, &got, NULL)) return -1;
            return (int)got;
        }
        if (!pf_proc_alive(p)) {
            /* drain race: process exited after last peek */
            if (PeekNamedPipe(p->out_r, NULL, 0, NULL, &avail, NULL) && avail > 0) continue;
            return -1;
        }
        if (GetTickCount64() >= deadline) return 0;
        Sleep(1);
    }
}

void pf_proc_close(PfProc *p, int graceful_ms) {
    if (!p) return;
    if (p->in_w) {
        CloseHandle(p->in_w);
        p->in_w = NULL;
    }
    if (p->process) {
        if (WaitForSingleObject(p->process, (DWORD)graceful_ms) != 0) {
            TerminateProcess(p->process, 1);
            WaitForSingleObject(p->process, 2000);
        }
        CloseHandle(p->process);
    }
    if (p->thread) CloseHandle(p->thread);
    if (p->out_r) CloseHandle(p->out_r);
    free(p);
}
