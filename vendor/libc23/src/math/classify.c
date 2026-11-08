/* Quiet binary classification by inspecting object representations. */
#include <math.h>
#include <stdint.h>
int __libc_fpclassifyf(float x) {
    union {float f; uint32_t u;} a={.f=x}; uint32_t v=a.u&0x7fffffffU, e=v>>23;
    if (e==255) return (v&0x7fffffU)?FP_NAN:FP_INFINITE;
    if (!e) return v?FP_SUBNORMAL:FP_ZERO;
    return FP_NORMAL;
}
int __libc_fpclassify(double x) {
    union {double f; uint64_t u;} a={.f=x}; uint64_t v=a.u&0x7fffffffffffffffULL, e=v>>52;
    if (e==2047) return (v&0xfffffffffffffULL)?FP_NAN:FP_INFINITE;
    if (!e) return v?FP_SUBNORMAL:FP_ZERO;
    return FP_NORMAL;
}
int __libc_fpclassifyl(long double x) {
    union {long double f; unsigned char b[sizeof(long double)];} a={.f=x};
#if LDBL_MANT_DIG == 53
    uint64_t u; __builtin_memcpy(&u,a.b,8); u&=0x7fffffffffffffffULL;
    unsigned e=(unsigned)(u>>52); int fraction=(u&0xfffffffffffffULL)!=0;
#elif LDBL_MANT_DIG == 64
    uint64_t u; __builtin_memcpy(&u,a.b,8);
    unsigned e=((unsigned)(a.b[9]&0x7f)<<8)|a.b[8]; int fraction=(u&0x7fffffffffffffffULL)!=0;
#else
    unsigned e=((unsigned)(a.b[15]&0x7f)<<8)|a.b[14]; int fraction=0;
    for (int i=0;i<14;i++) fraction |= a.b[i];
#endif
#if LDBL_MANT_DIG == 53
    if (e==2047) return fraction?FP_NAN:FP_INFINITE;
#else
    if (e==32767) return fraction?FP_NAN:FP_INFINITE;
#endif
    if (!e) return fraction?FP_SUBNORMAL:FP_ZERO;
    return FP_NORMAL;
}
