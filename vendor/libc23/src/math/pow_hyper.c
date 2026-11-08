/* Power and hyperbolic functions for binary64. Long double uses double accuracy. */
#include "libm.h"
double exp(double), expm1(double), log(double), log1p(double);
double __libc_pow_finite(double, double, int) LIBC_HIDDEN;

/* Integral/odd classification without a conversion that could overflow. */
static int integer_kind(double y){uint64_t u=asuint64(y)&0x7fffffffffffffffULL;int e=(int)(u>>52)-1023;if(e<0)return 0;if(e>52)return 2;uint64_t m=(u&0xfffffffffffffULL)|(1ULL<<52);unsigned shift=(unsigned)(52-e);if(shift&&(m&((1ULL<<shift)-1)))return 0;return ((m>>shift)&1)?1:2;}
static double signed_overflow(int sign){volatile double a=sign?-DBL_MAX:DBL_MAX,b=DBL_MAX;return a*b;}

double pow(double x,double y){
 uint64_t ux=asuint64(x),uy=asuint64(y),ax=ux&0x7fffffffffffffffULL,ay=uy&0x7fffffffffffffffULL;
 if(!ay||x==1.0)return 1.0;
 if(isnan(x)||isnan(y))return x+y;
 if(isinf(y)){if(ax==0x3ff0000000000000ULL)return 1.0;return ((ax>0x3ff0000000000000ULL)==(y>0))?INFINITY:0.0;}
 int k=integer_kind(y),negative=(ux>>63)&&k==1;
 if(!ax||ax==0x7ff0000000000000ULL){if(y<0){if(!ax)return __math_divzero(negative);return negative?-0.0:0.0;}if(!ax)return negative?-0.0:0.0;return negative?-INFINITY:INFINITY;}
 if((ux>>63)&&!k)return __math_invalid(x);
 double a=asdouble(ax);
 if(a==1.0)return negative?-1.0:1.0;
 if(y==1.0)return x;if(y==-1.0)return 1.0/x;if(y==2.0)return x*x;if(y==0.5&&!negative)return sqrt(a);
 return __libc_pow_finite(a,y,negative);
}

static const double ln2=0x1.62e42fefa39efp-1;
double sinh(double x){double a=fabs(x);if(!isfinite(x)||a==0)return x;if(a<0x1p-28)return x;if(a<22.0){double t=expm1(a);double r=0.5*(t+t/(t+1));return copysign(r,x);}if(a<709.0)return copysign(0.5*exp(a),x);if(a<711.0){double t=exp(a*0.5);double r=(0.5*t)*t;return copysign(r,x);}return signed_overflow(signbit(x));}
double cosh(double x){double a=fabs(x);if(isnan(x))return x+x;if(isinf(x))return INFINITY;if(a==0.0)return 1.0;if(a<0x1p-27){volatile double tiny=0x1p-55;return 1.0+tiny;}if(a<22.0){double t=expm1(a);return 1.0+t*t/(2.0*(1.0+t));}if(a<709.0)return 0.5*exp(a);if(a<711.0){double t=exp(a*0.5);return (0.5*t)*t;}return signed_overflow(0);}
double tanh(double x){double a=fabs(x);if(isnan(x))return x+x;if(isinf(x))return copysign(1.0,x);if(a==0)return x;if(a<0x1p-28)return x;if(a>20.0){volatile double tiny=0x1p-55;return signbit(x)?tiny-1.0:1.0-tiny;}double t=expm1(-2.0*a);return copysign(-t/(2.0+t),x);}
double asinh(double x){double a=fabs(x);if(!isfinite(x)||a==0)return x;if(a<0x1p-28)return x;double r;if(a>0x1p28)r=log(a)+ln2;else r=log1p(a+a*a/(1.0+sqrt(1.0+a*a)));return copysign(r,x);}
double acosh(double x){if(isnan(x))return x+x;if(x<1.0)return __math_invalid(x);if(x==1.0)return 0.0;if(isinf(x))return x;if(x>0x1p28)return log(x)+ln2;double t=x-1.0;return log1p(t+sqrt(t*(x+1.0)));}
double atanh(double x){double a=fabs(x);if(isnan(x))return x+x;if(a>1.0)return __math_invalid(x);if(a==1.0)return __math_divzero(signbit(x));if(a<0x1p-28)return x;return copysign(0.5*log1p((2.0*a)/(1.0-a)),x);}
#define UNARY(n) float n##f(float x){return (float)n((double)x);} long double n##l(long double x){return (long double)n((double)x);}
UNARY(sinh) UNARY(cosh) UNARY(tanh) UNARY(asinh) UNARY(acosh) UNARY(atanh)
float powf(float x,float y){return (float)pow((double)x,(double)y);}
long double powl(long double x,long double y){return (long double)pow((double)x,(double)y);}




