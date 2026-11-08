/* C23 7.24.9.1: lowest-address-set-bit alignment, or zero for null. */
#include <stdlib.h>
#include <stdint.h>

size_t memalignment(const void *p) {
    uintptr_t v = (uintptr_t)p;
    return (size_t)(v & (~v + 1));
}
