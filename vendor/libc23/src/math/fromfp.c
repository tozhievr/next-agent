/* C23 explicitly rounded floating-point integer values. */
#include <math.h>
#include <fenv.h>

struct rounded_value { long double value; int inexact; };

/* All arithmetic here is exact: the powers and accumulated integer are
 * representable in the input format, and subtraction removes set bits. */
static struct rounded_value rounded(long double x, int direction) {
    struct rounded_value r;
    int negative=signbit(x)!=0;
    long double a=negative ? -x : x, whole=0, fraction;
    int odd=0;
    if (a < 1) fraction=a;
    else {
        long double bit=1;
        while (bit <= a/2) bit*=2;
        do {
            if (a>=bit) { a-=bit; whole+=bit; if (bit==1) odd=1; }
            bit*=0.5L;
        } while (bit>=1);
        fraction=a;
    }
    r.inexact=fraction!=0;
    int increment = (direction==FP_INT_UPWARD && !negative && r.inexact) ||
                    (direction==FP_INT_DOWNWARD && negative && r.inexact) ||
                    (direction==FP_INT_TONEARESTFROMZERO && fraction>=0.5L) ||
                    (direction==FP_INT_TONEAREST && (fraction>0.5L ||
                        (fraction==0.5L && odd)));
    if (increment) whole+=1;
    r.value=negative ? -whole : whole;
    return r;
}

/* Compare an integral magnitude with 2^exponent without constructing a value
 * that might overflow the floating-point type. */
static int compare_power(long double a, unsigned exponent) {
    if (a==0) return -1;
    unsigned e=0;
    while (a>=2) { a*=0.5L; ++e; }
    if (e!=exponent) return e<exponent ? -1 : 1;
    return a==1 ? 0 : 1;
}
static long double convert(long double x, int direction, unsigned width, int unsig, int exact) {
    fenv_t saved;
    feholdexcept(&saved);
    int invalid=!isfinite(x) || !width;
    struct rounded_value r={0,0};
    if (!invalid) {
        r=rounded(x,direction);
        long double magnitude=signbit(r.value) ? -r.value : r.value;
        if (unsig) invalid=(r.value<0) || compare_power(magnitude,width)>=0;
        else {
            int cmp=compare_power(magnitude,width-1);
            invalid=signbit(r.value) ? cmp>0 : cmp>=0;
        }
    }
    fesetenv(&saved);
    if (invalid) { feraiseexcept(FE_INVALID); return NAN; }
    if (exact && r.inexact) feraiseexcept(FE_INEXACT);
    return r.value;
}
#define CONVERSIONS(suffix, type) \
type fromfp##suffix(type x,int d,unsigned w) { return (type)convert(x,d,w,0,0); } \
type ufromfp##suffix(type x,int d,unsigned w) { return (type)convert(x,d,w,1,0); } \
type fromfpx##suffix(type x,int d,unsigned w) { return (type)convert(x,d,w,0,1); } \
type ufromfpx##suffix(type x,int d,unsigned w) { return (type)convert(x,d,w,1,1); }
CONVERSIONS(,double)
CONVERSIONS(f,float)
CONVERSIONS(l,long double)

long double roundevenl(long double x) {
    if (!isfinite(x) || x==0) return x+x;
    fenv_t saved; feholdexcept(&saved);
    long double result=rounded(x,FP_INT_TONEAREST).value;
    fesetenv(&saved);
    return result;
}
double roundeven(double x) { return (double)roundevenl(x); }
float roundevenf(float x) { return (float)roundevenl(x); }
