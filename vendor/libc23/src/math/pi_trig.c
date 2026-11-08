#include <math.h>
/* C23 7.12.4.8-14: pi-scaled circular functions. */
double sin(double), cos(double), tan(double), asin(double), acos(double), atan(double), atan2(double,double);
static const double pi=0x1.921fb54442d18p+1;
static const double invpi=0x1.45f306dc9c883p-2;
static double invalid(double x) { volatile double v=x; return v-v; }
/* The remainder is exact: two is a power of two and no product by pi occurs
 * until the argument is reduced to [0, 1/2). */
static double reduce(double x, int *q) {
    double r=fmod(fabs(x),2.0);
    *q=(int)(2*r);
    return r-0.5*(*q);
}
double sinpi(double x) {
    if (!isfinite(x)) return isnan(x) ? x+x : invalid(x);
    int q; double t=reduce(x,&q);
    if (t==0) {
        if (!(q&1)) return copysign(0,x);
        double v=q==1?1.0:-1.0; return signbit(x)?-v:v;
    }
    if (t>0.25) { t-=0.5; q=(q+1)&3; }
    double a=pi*t, r=(q&1)?cos(a):sin(a);
    if (q>=2) r=-r;
    return signbit(x)?-r:r;
}
double cospi(double x) {
    if (!isfinite(x)) return isnan(x) ? x+x : invalid(x);
    int q; double t=reduce(x,&q);
    if (t==0) return (q&1)?0.0:(q==0?1.0:-1.0);
    if (t>0.25) { t-=0.5; q=(q+1)&3; }
    double a=pi*t, r=(q&1)?sin(a):cos(a);
    return (q==1||q==2)?-r:r;
}
double tanpi(double x) {
    if (!isfinite(x)) return isnan(x) ? x+x : invalid(x);
    int q; double t=reduce(x,&q);
    if (t==0) {
        int neg=signbit(x) != (q>=2);
        if (!(q&1)) return copysign(0,neg?-1.0:1.0);
        volatile double one=neg?-1.0:1.0, zero=0.0;
        return one/zero;
    }
    if (t>0.25) { t-=0.5; q=(q+1)&3; }
    double r=tan(pi*t);
    if (q&1) r=-1/r;
    return signbit(x)?-r:r;
}
double asinpi(double x) { if (x==1) return 0.5; if (x==-1) return -0.5; return asin(x)*invpi; }
double acospi(double x) { if (x==1) return 0; if (x==-1) return 1; if (x==0) return 0.5; return acos(x)*invpi; }
double atanpi(double x) { if (isinf(x)) return copysign(0.5,x); if (x==1||x==-1) return copysign(0.25,x); return atan(x)*invpi; }
double atan2pi(double y,double x) {
    if (isnan(x)||isnan(y)) return x+y;
    if (y==0) return signbit(x)?copysign(1,y):y;
    if (x==0) return copysign(0.5,y);
    if (isinf(x)) {
        if (isinf(y)) return copysign(signbit(x)?0.75:0.25,y);
        return copysign(signbit(x)?1.0:0.0,y);
    }
    if (isinf(y)) return copysign(0.5,y);
    if (fabs(x)==fabs(y)) return copysign(signbit(x)?0.75:0.25,y);
    return atan2(y,x)*invpi;
}
#define WRAP(n) float n##f(float x) { return (float)n(x); } long double n##l(long double x) { return (long double)n((double)x); }
WRAP(sinpi) WRAP(cospi) WRAP(tanpi) WRAP(asinpi) WRAP(acospi) WRAP(atanpi)
float atan2pif(float y,float x) { return (float)atan2pi(y,x); }
long double atan2pil(long double y,long double x) { return (long double)atan2pi((double)y,(double)x); }
