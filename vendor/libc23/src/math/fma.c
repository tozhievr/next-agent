/* Exact integer accumulation for binary32/binary64 fused multiply-add. */
#include "libm.h"
#include <fenv.h>

#define LIMBS 68
#define BASE_EXP (-2148)
typedef struct { uint64_t v[LIMBS]; } big;
static void bit_add(big *a, int bit) {
    unsigned i = (unsigned)bit / 64; uint64_t n = 1ULL << (bit % 64);
    while (i < LIMBS) { uint64_t old = a->v[i]; a->v[i] += n; if (a->v[i] >= old) break; n = 1; ++i; }
}
static int bit_get(const big *a, int b) { return b >= 0 && b < LIMBS * 64 ? (int)((a->v[b / 64] >> (b % 64)) & 1) : 0; }
static int compare(const big *a, const big *b) { for (int i = LIMBS-1; i >= 0; --i) if (a->v[i] != b->v[i]) return a->v[i] > b->v[i] ? 1 : -1; return 0; }
static void add(big *a, const big *b) { uint64_t carry = 0; for (int i=0;i<LIMBS;i++) { uint64_t x=a->v[i], y=b->v[i], t=x+y, c=t<x; uint64_t r=t+carry; carry=c | (r<t); a->v[i]=r; } }
static void sub(big *a, const big *b) { uint64_t borrow=0; for (int i=0;i<LIMBS;i++) { uint64_t x=a->v[i], y=b->v[i], t=x-y, c=x<y; uint64_t r=t-borrow; borrow=c | (t<borrow); a->v[i]=r; } }
static uint64_t mantissa(double x, int *e) {
    uint64_t u=asuint64(x), m=u & 0xfffffffffffffULL; int exp=(int)((u>>52)&2047);
    if (exp) { m |= 1ULL<<52; *e=exp-1075; } else *e=-1074;
    return m;
}
static uint64_t fused_bits(double x, double y, double z, int p, int emin, int emax, int bias) {
    big a={{0}}, b={{0}}; int ex,ey,ez;
    uint64_t mx=mantissa(x,&ex), my=mantissa(y,&ey), mz=mantissa(z,&ez);
    int sp=signbit(x)^signbit(y), sz=signbit(z), sign=sp;
    for (int i=0;i<53;i++) if ((mx>>i)&1) for (int j=0;j<53;j++) if ((my>>j)&1) bit_add(&a,ex+ey+i+j-BASE_EXP);
    for (int i=0;i<53;i++) if ((mz>>i)&1) bit_add(&b,ez+i-BASE_EXP);
    if (sp==sz) add(&a,&b); else { int c=compare(&a,&b); if (c<0) { sub(&b,&a); a=b; sign=sz; } else { sub(&a,&b); if (!c) sign=fegetround()==FE_DOWNWARD; } }
    int top=LIMBS*64-1; while (top>=0 && !bit_get(&a,top)) --top;
    if (top<0) { if (!mx || !my) if (!mz && sp==sz) sign=sp; return (uint64_t)sign << (p == 53 ? 63 : 31); }
    int e=top+BASE_EXP, cut=(e < emin ? emin : e) - (p-1) - BASE_EXP;
    uint64_t sig=0; for (int i=top;i>=cut;i--) sig=(sig<<1)|bit_get(&a,i);
    int half=bit_get(&a,cut-1), sticky=0; for(int i=cut-2;i>=0;i--) if(bit_get(&a,i)) { sticky=1; break; }
    int inexact=half|sticky, mode=fegetround();
    if ((mode==FE_TONEAREST && half && (sticky || (sig&1))) || (inexact && ((mode==FE_UPWARD && !sign)||(mode==FE_DOWNWARD && sign)))) ++sig;
    if (sig == (1ULL<<p)) { sig>>=1; ++e; }
    uint64_t signbitout=(uint64_t)sign << (p==53 ? 63:31);
    if (e>emax) {
        feraiseexcept(FE_OVERFLOW|FE_INEXACT);
        uint64_t inf=(uint64_t)(emax+bias+1) << (p-1);
        int infinity=mode==FE_TONEAREST || (mode==FE_UPWARD && !sign) || (mode==FE_DOWNWARD && sign);
        return signbitout | (infinity ? inf : inf-1);
    }
    if (inexact) { int flags=FE_INEXACT; if (sig < (1ULL<<(p-1))) flags |= FE_UNDERFLOW; feraiseexcept(flags); }
    if (sig < (1ULL<<(p-1))) return signbitout | sig;
    if (e<emin) e=emin;
    return signbitout | ((uint64_t)(e+bias)<<(p-1)) | (sig & ((1ULL<<(p-1))-1));
}
double fma(double x,double y,double z) {
    if (isfinite(x) && isfinite(y) && !isfinite(z)) return z + z;
    if (!isfinite(x)||!isfinite(y)) { volatile double a=x,b=y,c=z; return a*b+c; }
    return asdouble(fused_bits(x,y,z,53,-1022,1023,1023));
}
float fmaf(float x,float y,float z) {
    if (isfinite(x) && isfinite(y) && !isfinite(z)) return z + z;
    if (!isfinite(x)||!isfinite(y)) { volatile float a=x,b=y,c=z; return a*b+c; }
    return asfloat((uint32_t)fused_bits(x,y,z,24,-126,127,127));
}
long double fmal(long double x,long double y,long double z) { return (long double)fma((double)x,(double)y,(double)z); }
/* Internal binary64-input, binary32-output fused operation for C23 narrowing. */
float __libc_narrow_fma(double x, double y, double z) {
    if (!isfinite(x) || !isfinite(y) || !isfinite(z)) {
        if (isfinite(x) && isfinite(y)) { volatile double c=z; return (float)(c+c); }
        volatile double a=x,b=y,c=z; return (float)(a*b+c);
    }
    return asfloat((uint32_t)fused_bits(x,y,z,24,-126,127,127));
}
