#include "libc.h"

_Thread_local struct libc_tls __libc_tls;

int *__errno_location(void) {
    return &__libc_tls.err;
}
