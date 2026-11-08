/* Clang 128-bit integer to floating-point ABI, with no host CRT dependency. */
#if defined(__SIZEOF_INT128__)
#include <stdint.h>
#include <fenv.h>
typedef unsigned __int128 u128;
typedef __int128 s128;
static uint64_t encode(u128 n, int negative, int precision, int bias, int max_exp) {
    unsigned signshift=precision==24 ? 31 : 63;
    uint64_t sign=(uint64_t)negative<<signshift;
    if (!n) return sign;
    int exponent=0;
    for (u128 t=n; t>>1; t>>=1) ++exponent;
    int cut=exponent-(precision-1), inexact=0, half=0, sticky=0;
    uint64_t sig;
    if (cut<=0) sig=(uint64_t)(n<<(-cut));
    else {
        sig=(uint64_t)(n>>cut);
        half=(int)((n>>(cut-1))&1);
        sticky=cut>1 && (n & (((u128)1<<(cut-1))-1))!=0;
        inexact=half||sticky;
        int mode=fegetround();
        if ((mode==FE_TONEAREST && half && (sticky || (sig&1))) ||
            (inexact && ((mode==FE_UPWARD && !negative) || (mode==FE_DOWNWARD && negative)))) ++sig;
        if (sig==((uint64_t)1<<precision)) { sig>>=1; ++exponent; }
    }
    if (exponent>max_exp) {
        int mode=fegetround();
        int infinity=mode==FE_TONEAREST || (mode==FE_UPWARD && !negative) || (mode==FE_DOWNWARD && negative);
        uint64_t inf=(uint64_t)(max_exp+bias+1)<<(precision-1);
        feraiseexcept(FE_OVERFLOW|FE_INEXACT);
        return sign | (infinity ? inf : inf-1);
    }
    if (inexact) feraiseexcept(FE_INEXACT);
    return sign | ((uint64_t)(exponent+bias)<<(precision-1)) | (sig & (((uint64_t)1<<(precision-1))-1));
}
static double todouble(u128 x,int sign) { union { uint64_t u; double d; } v={.u=encode(x,sign,53,1023,1023)}; return v.d; }
static float tofloat(u128 x,int sign) { union { uint32_t u; float f; } v={.u=(uint32_t)encode(x,sign,24,127,127)}; return v.f; }
double __floatuntidf(u128 x) { return todouble(x,0); }
float __floatuntisf(u128 x) { return tofloat(x,0); }
double __floattidf(s128 x) { u128 u=(u128)x; int neg=x<0; return todouble(neg ? 0-u : u,neg); }
float __floattisf(s128 x) { u128 u=(u128)x; int neg=x<0; return tofloat(neg ? 0-u : u,neg); }
#endif
