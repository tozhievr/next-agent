/* <stdio.h> - C23 7.23 (libc23). */
#ifndef _LIBC23_STDIO_H
#define _LIBC23_STDIO_H

#define __STDC_VERSION_STDIO_H__ 202311L

#define __need_size_t
#define __need_NULL
#include <stddef.h>
#define __need___va_list
#include <stdarg.h>
#include <bits/mbstate.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __FILE FILE;

typedef struct __fpos {
    long long __off;
    mbstate_t __mbs;
} fpos_t;

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define BUFSIZ        8192
#define EOF           (-1)
#define FOPEN_MAX     64
#define FILENAME_MAX  4096
#define L_tmpnam      272
#define TMP_MAX       2147483647
#define _PRINTF_NAN_LEN_MAX 4

#define SEEK_CUR 1
#define SEEK_END 2
#define SEEK_SET 0

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#define stdin  stdin
#define stdout stdout
#define stderr stderr

#ifdef LIBC23_ENABLE_HOST_FORMAT_DIAGNOSTICS
#define __LIBC23_PRINTF(a, b) __attribute__((__format__(__printf__, a, b)))
#define __LIBC23_SCANF(a, b)  __attribute__((__format__(__scanf__, a, b)))
#else
#define __LIBC23_PRINTF(a, b)
#define __LIBC23_SCANF(a, b)
#endif

int   remove(const char *__filename);
int   rename(const char *__old, const char *__new);
FILE *tmpfile(void);
char *tmpnam(char *__s);

int   fclose(FILE *__stream);
int   fflush(FILE *__stream);
FILE *fopen(const char *restrict __filename, const char *restrict __mode);
FILE *freopen(const char *restrict __filename, const char *restrict __mode,
              FILE *restrict __stream);
void  setbuf(FILE *restrict __stream, char *restrict __buf);
int   setvbuf(FILE *restrict __stream, char *restrict __buf, int __mode, size_t __size);

int fprintf(FILE *restrict __stream, const char *restrict __format, ...) __LIBC23_PRINTF(2, 3);
int fscanf(FILE *restrict __stream, const char *restrict __format, ...) __LIBC23_SCANF(2, 3);
int printf(const char *restrict __format, ...) __LIBC23_PRINTF(1, 2);
int scanf(const char *restrict __format, ...) __LIBC23_SCANF(1, 2);
int snprintf(char *restrict __s, size_t __n, const char *restrict __format, ...) __LIBC23_PRINTF(3, 4);
int sprintf(char *restrict __s, const char *restrict __format, ...) __LIBC23_PRINTF(2, 3);
int sscanf(const char *restrict __s, const char *restrict __format, ...) __LIBC23_SCANF(2, 3);
int vfprintf(FILE *restrict __stream, const char *restrict __format, __gnuc_va_list __arg) __LIBC23_PRINTF(2, 0);
int vfscanf(FILE *restrict __stream, const char *restrict __format, __gnuc_va_list __arg) __LIBC23_SCANF(2, 0);
int vprintf(const char *restrict __format, __gnuc_va_list __arg) __LIBC23_PRINTF(1, 0);
int vscanf(const char *restrict __format, __gnuc_va_list __arg) __LIBC23_SCANF(1, 0);
int vsnprintf(char *restrict __s, size_t __n, const char *restrict __format, __gnuc_va_list __arg) __LIBC23_PRINTF(3, 0);
int vsprintf(char *restrict __s, const char *restrict __format, __gnuc_va_list __arg) __LIBC23_PRINTF(2, 0);
int vsscanf(const char *restrict __s, const char *restrict __format, __gnuc_va_list __arg) __LIBC23_SCANF(2, 0);

int   fgetc(FILE *__stream);
char *fgets(char *restrict __s, int __n, FILE *restrict __stream);
int   fputc(int __c, FILE *__stream);
int   fputs(const char *restrict __s, FILE *restrict __stream);
int   getc(FILE *__stream);
int   getchar(void);
int   putc(int __c, FILE *__stream);
int   putchar(int __c);
int   puts(const char *__s);
int   ungetc(int __c, FILE *__stream);

size_t fread(void *restrict __ptr, size_t __size, size_t __nmemb, FILE *restrict __stream);
size_t fwrite(const void *restrict __ptr, size_t __size, size_t __nmemb, FILE *restrict __stream);

int  fgetpos(FILE *restrict __stream, fpos_t *restrict __pos);
int  fseek(FILE *__stream, long __offset, int __whence);
int  fsetpos(FILE *__stream, const fpos_t *__pos);
long ftell(FILE *__stream);
void rewind(FILE *__stream);

void clearerr(FILE *__stream);
int  feof(FILE *__stream);
int  ferror(FILE *__stream);
void perror(const char *__s);

#ifdef __cplusplus
}
#endif
#endif /* _LIBC23_STDIO_H */
