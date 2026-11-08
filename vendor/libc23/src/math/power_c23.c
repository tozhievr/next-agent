/* C23 power/root functions. Long double follows the library's double-accuracy ABI. */
#include "libm.h"
double exp(double), expm1(double), log(double), log1p(double);
double __libc_pow_finite(double,double,int) LIBC_HIDDEN;
static double compound_scaled(double,double,long long);

double pown(double x,long long n){
 if(!n) {
  uint64_t bits=asuint64(x)&0x7fffffffffffffffULL;
  if(bits>0x7ff0000000000000ULL && !(bits&0x0008000000000000ULL)) return x+x;
  return 1.0;
 }
 if(isnan(x))return x+x;
 int neg=signbit(x)&&((unsigned long long)n&1);double a=fabs(x);
 if(a==0.0){if(n<0)return __math_divzero(neg);return neg?-0.0:0.0;}
 if(isinf(a)){if(n<0)return neg?-0.0:0.0;return neg?-INFINITY:INFINITY;}
 if(a==1.0)return neg?-1.0:1.0;
 if(n==1)return x;if(n==-1)return 1.0/x;if(n==2)return x*x;
 if(n > 9007199254740992LL || n < -9007199254740992LL) {
  double result=compound_scaled(a,0.0,n);
  return neg ? -result : result;
 }
 return __libc_pow_finite(a,(double)n,neg);
}
/* A positive double-double significand with an independently scaled exponent.
 * The exponent is saturated far beyond binary64's representable range. */
struct scaled_dd { double hi, lo; int exponent; };
static int scaled_exp(int a,int b){int n=a+b;return n>10000?10000:n< -10000?-10000:n;}
static struct scaled_dd scaled_mul(struct scaled_dd a,struct scaled_dd b){
 double h=a.hi*b.hi;
 double l=fma(a.hi,b.hi,-h)+a.hi*b.lo+a.lo*b.hi+a.lo*b.lo;
 double sum=h+l; l=(h-sum)+l;
 int shift;double normalized=frexp(sum,&shift);
 struct scaled_dd r={normalized,scalbn(l,-shift),scaled_exp(scaled_exp(a.exponent,b.exponent),shift)};
 return r;
}
static double compound_scaled(double base,double low,long long n){
 int e;double h=frexp(base,&e), l=scalbn(low,-e);
 struct scaled_dd b={h,l,e}, result={0.5,0.0,1};
 unsigned long long count=(unsigned long long)n;
 if(n<0){
  count=0ULL-count;
  double q=1.0/h;
  double ql=(fma(-q,h,1.0)-q*l)/h;
  double s=q+ql;ql=(q-s)+ql;
  int shift;double normal=frexp(s,&shift);
  b.hi=normal;b.lo=scalbn(ql,-shift);b.exponent=-e+shift;
 }
 while(count){if(count&1)result=scaled_mul(result,b);count>>=1;if(count)b=scaled_mul(b,b);}
 return scalbn(result.hi+result.lo,result.exponent);
}
/* For tiny x, retain the square and cube terms of log1p(x) separately
 * from n*x.  Splitting n keeps all 64 integer bits. */
static double compound_tiny(double x,long long n){
 const long long unit=4294967296LL;
 double nh=(double)(n/unit)*0x1p32, nl=(double)(n%unit);
 double h=nh*x;
 double tail=x*x*(-0.5+x/3.0);
 double l=fma(nh,x,-h)+nl*x+nh*tail+nl*tail;
 if(h>1400.0||h< -1500.0)return exp(h);
 if(h>700.0||h< -700.0){double t=exp(h*0.5);return fma(t,expm1(l),t)*t;}
 double r=exp(h);return fma(r,expm1(l),r);
}
double compoundn(double x,long long n){
 if(x< -1.0)return __math_invalid(x);
 if(!n) {
  uint64_t bits=asuint64(x)&0x7fffffffffffffffULL;
  if(bits>0x7ff0000000000000ULL && !(bits&0x0008000000000000ULL)) return x+x;
  return 1.0;
 }
 if(isnan(x))return x+x;
 if(x==-1.0)return n<0?__math_divzero(0):0.0;
 if(isinf(x))return n<0?0.0:INFINITY;
 if(x==0.0)return 1.0;
 if(n==1)return 1.0+x;
 if(fabs(x)<0x1p-26)return compound_tiny(x,n);
 /* TwoSum retains the exact low part even when the rounded sum is one. */
 double base=1.0+x, z=base-1.0;
 double low=(1.0-(base-z))+(x-z);
 if(!isfinite(base))return exp((double)n*log1p(x));
 if(low==0.0)return pown(base,n);
 return compound_scaled(base,low,n);
}
double powr(double x,double y){
 if(x<0.0)return __math_invalid(0.0);
 if(isnan(x)||isnan(y))return x+y;
 if( (x==0.0&&y==0.0) || (isinf(x)&&y==0.0) || (x==1.0&&isinf(y)))return __math_invalid(0.0);
 return pow(fabs(x),y);
}
double rootn(double x,long long n){
 if(!n)return __math_invalid(0.0);
 if(isnan(x))return x+x;
 int odd=(int)((unsigned long long)n&1), neg=signbit(x)&&odd;double a=fabs(x);
 if(x<0.0&&!odd)return __math_invalid(x);
 if(a==0.0){if(n<0)return __math_divzero(neg);return neg?-0.0:0.0;}
 if(isinf(a)){if(n<0)return neg?-0.0:0.0;return neg?-INFINITY:INFINITY;}
 if(n==1)return x;if(n==-1)return 1.0/x;
 if(n==2)return sqrt(x);if(n==-2)return 1.0/sqrt(x);
 if(a==1.0)return neg?-1.0:1.0;
 /* A power-of-two input with an integral root exponent has an exact root. */
 if(a<INFINITY && n>=-1074 && n<=1074){
  int e;
  if(frexp(a,&e)==0.5){
   int p=e-1;
   if(p%n==0){double exact=scalbn(1.0,p/(int)n);return neg?-exact:exact;}
  }
 }
 double dn=(double)n, reciprocal=1.0/dn;
 double r=__libc_pow_finite(a,reciprocal,neg);
 /* Recover the low part of 1/n before exponentiating log(a). */
 double residual=fma(-reciprocal,dn,1.0)/dn;
 return fma(r,expm1(log(a)*residual),r);
}
double rsqrt(double x){if(isnan(x))return x+x;if(x<0.0)return __math_invalid(x);if(x==0.0)return __math_divzero(signbit(x));if(isinf(x))return 0.0;return 1.0/sqrt(x);}
#define UNARY(n) float n##f(float x){return (float)n((double)x);} long double n##l(long double x){return (long double)n((double)x);}
#define INTPOWER(n) float n##f(float x,long long p){return (float)n((double)x,p);} long double n##l(long double x,long long p){return (long double)n((double)x,p);}
INTPOWER(pown) INTPOWER(compoundn) INTPOWER(rootn)
UNARY(rsqrt)
float powrf(float x,float y){return (float)powr((double)x,(double)y);}
long double powrl(long double x,long double y){return (long double)powr((double)x,(double)y);}








