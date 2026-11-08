/*
 * Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
 * Permission to use, copy, modify, and distribute this software is freely
 * granted, provided that this notice is preserved.
 * Adapted from fdlibm e_pow.c, https://netlib.org/fdlibm/e_pow.c.
 */
#include "libm.h"
static double hi(double x){return asdouble(asuint64(x)&0xffffffff00000000ULL);}
static double with_hi(double x,uint32_t h){return asdouble(((uint64_t)h<<32)|(uint32_t)asuint64(x));}
static const double bp[]={1.0,1.5},dph[]={0,5.84962487220764160156e-01},dpl[]={0,1.35003920212974897128e-08};
static const double l1=5.99999999999994648725e-01,l2=4.28571428578550184252e-01,l3=3.33333329818377432918e-01,l4=2.72728123808534006489e-01,l5=2.30660745775561754067e-01,l6=2.06975017800338417784e-01;
static const double p1=1.66666666666666019037e-01,p2=-2.77777777770155933842e-03,p3=6.61375632143793436117e-05,p4=-1.65339022054652515390e-06,p5=4.13813679705723846039e-08;
static const double lg2=6.93147180559945286227e-01,lg2h=6.93147182464599609375e-01,lg2l=-1.90465429995776804525e-09,ovt=8.0085662595372944372e-17;
static const double cp=9.61796693925975554329e-01,cph=9.61796700954437255859e-01,cpl=-7.02846165095275826516e-09,ivln2=1.44269504088896338700e+00,ivln2h=1.44269502162933349609e+00,ivln2l=1.92596299112661746887e-08;
static double over(int sign){volatile double a=sign?-DBL_MAX:DBL_MAX,b=DBL_MAX;return a*b;}
static double under(int sign){volatile double a=sign?-DBL_MIN:DBL_MIN,b=DBL_MIN;return a*b;}
/* x is finite and positive, y is finite and nonzero. */
double __libc_pow_finite(double x,double y,int negative){
 double z,zh,zl,ph,pl,y1,t1,t2,r,s,t,u,v,w;int n,k;uint32_t ix=(uint32_t)(asuint64(x)>>32),iy=(uint32_t)(asuint64(y)>>32)&0x7fffffff;
 if(iy>0x41e00000){if(iy>0x43f00000){if(ix<=0x3fefffff)return y<0?over(negative):under(negative);if(ix>=0x3ff00000)return y>0?over(negative):under(negative);}if(ix<0x3fefffff)return y<0?over(negative):under(negative);if(ix>0x3ff00000)return y>0?over(negative):under(negative);t=x-1.0;w=t*t*(0.5-t*(0.3333333333333333333333-t*0.25));u=ivln2h*t;v=t*ivln2l-w*ivln2;t1=hi(u+v);t2=v-(t1-u);
 }else{double ss,s2,sh,sl,th,tl;n=0;if(ix<0x00100000){x*=0x1p53;n-=53;ix=(uint32_t)(asuint64(x)>>32);}n+=(int)(ix>>20)-1023;uint32_t j=ix&0x000fffff;ix=j|0x3ff00000;if(j<=0x3988e)k=0;else if(j<0xbb67a)k=1;else{k=0;n++;ix-=0x00100000;}x=with_hi(x,ix);u=x-bp[k];v=1.0/(x+bp[k]);ss=u*v;sh=hi(ss);th=with_hi(0.0,((ix>>1)|0x20000000)+0x00080000+((uint32_t)k<<18));tl=x-(th-bp[k]);sl=v*((u-sh*th)-sh*tl);s2=ss*ss;r=s2*s2*(l1+s2*(l2+s2*(l3+s2*(l4+s2*(l5+s2*l6)))));r+=sl*(sh+ss);s2=sh*sh;th=hi(3.0+s2+r);tl=r-((th-3.0)-s2);u=sh*th;v=sl*th+tl*ss;ph=hi(u+v);pl=v-(ph-u);zh=cph*ph;zl=cpl*ph+pl*cp+dpl[k];t=(double)n;t1=hi(((zh+zl)+dph[k])+t);t2=zl-(((t1-t)-dph[k])-zh);}
 y1=hi(y);pl=(y-y1)*t1+y*t2;ph=y1*t1;z=pl+ph;if(z>=1024.0){if(z>1024.0||pl+ovt>z-ph)return over(negative);}else if(z<=-1075.0){if(z< -1075.0||pl<=z-ph)return under(negative);}
 n=0;if(fabs(z)>0.5){n=(int)(z+(z<0?-0.5:0.5));ph-=(double)n;}t=hi(pl+ph);u=t*lg2h;v=(pl-(t-ph))*lg2+t*lg2l;z=u+v;w=v-(z-u);t=z*z;t1=z-t*(p1+t*(p2+t*(p3+t*(p4+t*p5))));r=z*t1/(t1-2.0)-(w+z*w);z=1.0-(r-z);s=negative?-z:z;return scalbn(s,n);
}
