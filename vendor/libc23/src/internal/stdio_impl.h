/*
 * stdio_impl.h - internal interface between the stdio core (src/stdio/file*.c,
 * owner: "stdio-core") and the formatted I/O engines (src/stdio/fmt_*.c,
 * owner: "stdio-format").  The layout of struct __FILE is private to the core.
 *
 * All *_unlocked functions must be called with the stream locked via
 * __libc_flockfile().  Public stdio functions lock internally.
 * "wide" functions require/establish wide orientation, byte functions byte
 * orientation, exactly as C23 7.23.2 describes (fwide semantics).
 */
#ifndef LIBC23_STDIO_IMPL_H
#define LIBC23_STDIO_IMPL_H

#include <stdio.h>
#include "libc.h"

/* same typedef as <wchar.h>/<wctype.h> (repeated typedefs are valid C11+) */
typedef unsigned int wint_t;
#ifndef WEOF
#define WEOF ((wint_t)0xFFFFFFFFu)
#endif

void   __libc_flockfile(FILE *f)   LIBC_HIDDEN;
void   __libc_funlockfile(FILE *f) LIBC_HIDDEN;

/* byte stream primitives (stream must have, or acquire, byte orientation) */
int    __libc_getc_unlocked(FILE *f)           LIBC_HIDDEN; /* byte 0..255 or EOF */
int    __libc_ungetc_unlocked(int c, FILE *f)  LIBC_HIDDEN; /* c or EOF           */
int    __libc_putc_unlocked(int c, FILE *f)    LIBC_HIDDEN; /* c or EOF           */
size_t __libc_fwrite_unlocked(const void *p, size_t n, FILE *f) LIBC_HIDDEN; /* bytes written */
size_t __libc_fread_unlocked(void *p, size_t n, FILE *f)        LIBC_HIDDEN; /* bytes read    */

/* wide stream primitives (stream must have, or acquire, wide orientation) */
wint_t __libc_getwc_unlocked(FILE *f)              LIBC_HIDDEN; /* WEOF on end/error */
wint_t __libc_ungetwc_unlocked(wint_t c, FILE *f)  LIBC_HIDDEN;
wint_t __libc_putwc_unlocked(wchar_t c, FILE *f)   LIBC_HIDDEN;

/* Sets orientation if still unset (orient > 0 wide, < 0 byte); returns the
 * resulting orientation (fwide semantics, 0 = none yet). */
int    __libc_fwide_unlocked(FILE *f, int orient)  LIBC_HIDDEN;

int    __libc_ferror_unlocked(FILE *f) LIBC_HIDDEN;
int    __libc_feof_unlocked(FILE *f)   LIBC_HIDDEN;

int __libc_wide_getbyte_unlocked(FILE *f);
int __libc_wide_putbyte_unlocked(int c, FILE *f);
int __libc_wide_unget_bytes_unlocked(const unsigned char *s, size_t n, FILE *f);
void __libc_wide_error_unlocked(FILE *f);

#endif /* LIBC23_STDIO_IMPL_H */


