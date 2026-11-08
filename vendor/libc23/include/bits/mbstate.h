/* Shared definition of mbstate_t (used by <wchar.h>, <uchar.h>, <stdio.h>). */
#ifndef _LIBC23_BITS_MBSTATE_H
#define _LIBC23_BITS_MBSTATE_H

/* Opaque conversion state.  Initial state == all zero bits.
 *   __s[0]  bits of an incomplete multibyte character / pending surrogate
 *   __s[1]  library private (number of bytes collected, shift state, ...)   */
typedef struct __mbstate {
    unsigned int __s[2];
} mbstate_t;

#endif
