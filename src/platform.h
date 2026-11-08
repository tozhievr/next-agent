/* NextAgent: Windows platform layer (no Windows SDK required; kernel32 only). */
#ifndef NA_PLATFORM_H
#define NA_PLATFORM_H
#include "util.h"

/* Files. read returns 0 = ok, 1 = does not exist, -1 = error. */
int pf_read_file(const char *path, Buf *out);
int pf_read_from(const char *path, uint64_t offset, Buf *out); /* same codes */
bool pf_is_file(const char *path);
bool pf_is_dir(const char *path);
bool pf_is_reparse(const char *path); /* symlink / junction */
int pf_mkdirs(const char *dir);
int pf_write_atomic(const char *path, const void *data, size_t n);
/* Append `data` at byte offset `at` (truncating any torn tail), then flush to disk. */
int pf_append_at(const char *path, uint64_t at, const void *data, size_t n);
int pf_remove(const char *path);
char *pf_fullpath(const char *path); /* absolute, '/' separated, normalized */
/* List immediate entries; reparse points are skipped. */
int pf_list_dir(const char *dir, StrList *files, StrList *dirs);

/* Cross-process mutual exclusion via an exclusively opened lock file. */
typedef struct PfLock PfLock;
PfLock *pf_lock_acquire(const char *path, int timeout_ms);
void pf_lock_release(PfLock *lock);

/* Time. */
uint64_t pf_now_ms(void);
void pf_sleep_ms(unsigned ms);

/* Child process with piped stdin/stdout (stderr discarded). */
typedef struct PfProc PfProc;
char *pf_which(const char *exe); /* full path or NULL */
PfProc *pf_spawn(const char *exe, const char *args, const char *cwd);
int pf_proc_write(PfProc *p, const void *data, size_t n);
/* >0: bytes read; 0: timeout; -1: stream closed / process gone. */
int pf_proc_read(PfProc *p, void *buf, size_t cap, int timeout_ms);
bool pf_proc_alive(PfProc *p);
void pf_proc_close(PfProc *p, int graceful_ms);

/* Plain TCP client for the embedding backend: IPv4 literal or "localhost" only (no DNS, no TLS). */
typedef struct PfSock PfSock;
PfSock *pf_tcp_connect(const char *host, int port, int timeout_ms);
int pf_sock_write(PfSock *k, const void *data, size_t n); /* 0 ok */
/* >0: bytes read; 0: timeout; -1: closed / error. */
int pf_sock_read(PfSock *k, void *buf, size_t cap, int timeout_ms);
void pf_sock_close(PfSock *k);

#endif
