/* C permits an implementation-defined interpretation of the tag.  This
 * implementation returns the canonical quiet NaN for every tag. */
#include <stdint.h>

double nan(const char *tag) {
    (void)tag;
    union { uint64_t bits; double value; } u = { .bits = UINT64_C(0x7ff8000000000000) };
    return u.value;
}

float nanf(const char *tag) {
    (void)tag;
    union { uint32_t bits; float value; } u = { .bits = UINT32_C(0x7fc00000) };
    return u.value;
}

long double nanl(const char *tag) {
    return (long double)nan(tag);
}
