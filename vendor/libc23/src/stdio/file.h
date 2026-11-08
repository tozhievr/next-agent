/* file.h - private definition of struct __FILE (stdio core only). */
#ifndef LIBC23_STDIO_FILE_H
#define LIBC23_STDIO_FILE_H

#include "../internal/stdio_impl.h"

#define F_RD      0x0001u  /* readable                                   */
#define F_WR      0x0002u  /* writable                                   */
#define F_APP     0x0004u  /* opened for append                          */
#define F_EOF     0x0008u  /* end-of-file indicator                      */
#define F_ERR     0x0010u  /* error indicator                            */
#define F_OWNBUF  0x0020u  /* buf was malloc'ed by us                    */
#define F_STATIC  0x0040u  /* struct is static (stdin/out/err)           */
#define F_LAZYFD  0x0080u  /* fd must still be fetched with pal_std_file */

enum { M_NONE = 0, M_READ = 1, M_WRITE = 2 };
#define BM_AUTO (-1)

#define UNGET_MAX 8

struct __FILE {
    unsigned        flags;
    int             orient;            /* 0 none, >0 wide, <0 byte            */
    int             wide_utf8;          /* encoding captured at wide orientation */
    pal_file_t      fd;
    int             stdno;             /* 0..2 for std streams (lazy fd)      */
    int             mode;              /* M_NONE / M_READ / M_WRITE           */
    int             bufmode;           /* _IOFBF/_IOLBF/_IONBF or BM_AUTO     */
    unsigned char  *buf;
    size_t          bufsize;
    unsigned char  *rpos, *rend;       /* unread window of buf (M_READ)       */
    unsigned char  *wpos;              /* pending output is buf..wpos (M_WRITE) */
    unsigned char   unget[UNGET_MAX];  /* ungetc stack                        */
    int             ucnt;
    wchar_t         wunget[UNGET_MAX]; /* ungetwc stack                       */
    int             wucnt;
    mbstate_t       mbs;
    libc_rlock_t    lock;
    FILE           *prev, *next;       /* open-file list                      */
    char           *path;              /* copy of the file name (freopen)     */
    unsigned char   onebuf[1];
};

#define FD(f) (((f)->flags & F_LAZYFD) ? __stdio_resolve_fd(f) : (f)->fd)

pal_file_t __stdio_resolve_fd(FILE *f) LIBC_HIDDEN;
int     __stdio_flushw(FILE *f)        LIBC_HIDDEN;  /* flush pending output; 0 / EOF */
int     __stdio_to_read(FILE *f)       LIBC_HIDDEN;
int     __stdio_to_write(FILE *f)      LIBC_HIDDEN;
int     __stdio_refill(FILE *f)        LIBC_HIDDEN;  /* read more data; 0 / EOF       */
void    __stdio_drop_read(FILE *f)     LIBC_HIDDEN;  /* resync OS position, discard input */
int64_t __stdio_tell(FILE *f)          LIBC_HIDDEN;  /* logical position or -1        */
int     __stdio_seek(FILE *f, int64_t off, int whence) LIBC_HIDDEN;
size_t  __stdio_write_bytes(FILE *f, const unsigned char *p, size_t n) LIBC_HIDDEN;
size_t  __stdio_wide_unget_bytes(const FILE *f) LIBC_HIDDEN;
void    __stdio_ofl_add(FILE *f)       LIBC_HIDDEN;
void    __stdio_ofl_remove(FILE *f)    LIBC_HIDDEN;
FILE   *__stdio_new_file(void)         LIBC_HIDDEN;
int     __stdio_parse_mode(const char *mode, int *palflags, unsigned *fflags) LIBC_HIDDEN;

/* heap interface (stdlib module) */
void *malloc(size_t);
void  free(void *);

/* single byte read without orientation handling */
static inline int __stdio_getb(FILE *f) {
    if (f->ucnt) return f->unget[--f->ucnt];
    if (f->mode == M_READ && f->rpos < f->rend) return *f->rpos++;
    if (__stdio_refill(f)) return EOF;
    return *f->rpos++;
}

#endif



