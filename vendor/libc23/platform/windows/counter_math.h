#ifndef LIBC23_WINDOWS_COUNTER_MATH_H
#define LIBC23_WINDOWS_COUNTER_MATH_H
#include <stdint.h>

/* floor(remainder * 1e9 / frequency), where remainder < frequency.
 * Reduced remainders avoid overflow and 128-bit division runtime helpers. */
static inline int32_t counter_nanoseconds(uint64_t remainder, uint64_t frequency) {
    uint64_t r = 0;
    uint32_t q = 0;
    for (int bit = 29; bit >= 0; --bit) {
        q *= 2;
        if (r >= frequency - r) { r -= frequency - r; ++q; }
        else r += r;
        if ((1000000000u >> bit) & 1u) {
            if (r >= frequency - remainder) { r -= frequency - remainder; ++q; }
            else r += remainder;
        }
    }
    return (int32_t)q;
}
#endif
