/*
 * Adapted from fdlibm e_hypot.c and s_cbrt.c.
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 */
#include "libm.h"

double hypot(double x, double y) {
    double a=fabs(x), b=fabs(y), t1, t2, y1, y2, w;
    uint32_t ha=(uint32_t)(asuint64(a)>>32), hb=(uint32_t)(asuint64(b)>>32);
    if (ha < hb) { double t=a; a=b; b=t; uint32_t h=ha; ha=hb; hb=h; }
    if (ha >= 0x7ff00000U) {
        volatile double sum = a+b; /* Evaluate signaling NaNs even when the result is infinity. */
        if (asuint64(a)==0x7ff0000000000000ULL || asuint64(b)==0x7ff0000000000000ULL) return INFINITY;
        return sum;
    }
    if (ha-hb > 0x03c00000U) return a+b;
    int k=0;
    if (ha > 0x5f300000U) {
        a=asdouble(asuint64(a)-((uint64_t)0x25800000U<<32));
        b=asdouble(asuint64(b)-((uint64_t)0x25800000U<<32));
        k+=600;
    }
    if ((uint32_t)(asuint64(b)>>32) < 0x20b00000U) {
        if (b==0) return a;
        if ((uint32_t)(asuint64(b)>>32) <= 0x000fffffU) { a*=0x1p1022; b*=0x1p1022; k-=1022; }
        else { a*=0x1p600; b*=0x1p600; k-=600; }
    }
    ha=(uint32_t)(asuint64(a)>>32); hb=(uint32_t)(asuint64(b)>>32);
    w=a-b;
    if (w>b) {
        t1=asdouble((uint64_t)ha<<32); t2=a-t1;
        w=sqrt(t1*t1-(b*(-b)-t2*(a+t1)));
    } else {
        a=a+a; y1=asdouble((uint64_t)hb<<32); y2=b-y1;
        t1=asdouble((uint64_t)(ha+0x00100000U)<<32); t2=a-t1;
        w=sqrt(t1*y1-(w*(-w)-(t1*y2+t2*b)));
    }
    return k ? scalbn(w,k) : w;
}
float hypotf(float x,float y) { return (float)hypot((double)x,(double)y); }
long double hypotl(long double x,long double y) { return (long double)hypot((double)x,(double)y); }

double cbrt(double x) {
    uint64_t ux=asuint64(x), sign=ux&0x8000000000000000ULL;
    uint32_t hx=(uint32_t)((ux>>32)&0x7fffffffU);
    if (hx>=0x7ff00000U) return x+x;
    if ((ux<<1)==0) return x;
    x=asdouble(ux&0x7fffffffffffffffULL);
    double t;
    if (hx<0x00100000U) { t=0x1p54*x; t=asdouble((uint64_t)(((uint32_t)(asuint64(t)>>32))/3+696219795U)<<32); }
    else t=asdouble((uint64_t)(hx/3+715094163U)<<32);
    double r=t*t/x;
    double s=5.42857142857142815906e-01+r*t;
    t*=3.57142857142857150787e-01+1.60714285714285720630e+00/(s+1.41428571428571436819e+00-7.05306122448979611050e-01/s);
    t=asdouble(((asuint64(t)>>32)+1)<<32);
    s=t*t; r=x/s;
    double w=t+t;
    r=(r-t)/(w+r);
    t=t+t*r;
    return asdouble(asuint64(t)|sign);
}
float cbrtf(float x) { return (float)cbrt((double)x); }
long double cbrtl(long double x) { return (long double)cbrt((double)x); }
