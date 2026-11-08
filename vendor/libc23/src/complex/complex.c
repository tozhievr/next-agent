#include <complex.h>
#include <math.h>
#include <float.h>
#include <fenv.h>

/* Real functions supplied by the math modules. */
double sin(double), cos(double), sinh(double), cosh(double), exp(double), log(double), log1p(double), atan2(double,double), asin(double), acos(double), asinh(double), acosh(double), atanh(double);
static double complex zmake(double x, double y) { return CMPLX(x, y); }
static double re(double complex z) { return __real__ z; }
static double im(double complex z) { return __imag__ z; }
static double complex mul(double complex a, double complex b) { return zmake(re(a)*re(b)-im(a)*im(b), re(a)*im(b)+im(a)*re(b)); }
static double mag(double x, double y) {
    x=fabs(x); y=fabs(y); if (isinf(x)||isinf(y)) return INFINITY;
    if (isnan(x)||isnan(y)) return NAN;
    if (x<y) { double t=x; x=y; y=t; }
    if (!x) return 0;
    double t=y/x; return x*sqrt(1+t*t);
}
static double exp_product(double x, double factor) {
    if (factor==0) return factor;
    if (x>700 && isfinite(x)) {
        if (x>1500) {
            volatile double huge = copysign(0x1p1023, factor);
            volatile double scale = 0x1p1023;
            return huge * scale;
        }
        int fe; double m=frexp(factor,&fe);
        int k=(int)(x*1.44269504088896340736);
        double r=(x-k*0x1.62e42fee00000p-1)-k*1.9082149292705877e-10;
        return scalbn(exp(r)*m,k+fe);
    }
    return exp(x)*factor;
}
static double logmag(double x, double y) {
    x=fabs(x); y=fabs(y);
    if (isinf(x)||isinf(y)) return INFINITY;
    if (isnan(x)||isnan(y)) return NAN;
    double hi=x>y?x:y, lo=x>y?y:x;
    if (hi==0) return log(0);
    if (hi>DBL_MAX/2 || hi<DBL_MIN) {
        double t=lo/hi;
        return log(hi)+0.5*log1p(t*t);
    }
    return log(mag(x,y));
}
double creal(double complex z) { return re(z); }
double cimag(double complex z) { return im(z); }
double complex conj(double complex z) { return zmake(re(z),-im(z)); }
double complex cproj(double complex z) { return isinf(re(z))||isinf(im(z)) ? zmake(INFINITY,copysign(0,im(z))) : z; }
double cabs(double complex z) { return mag(re(z),im(z)); }
double carg(double complex z) { return atan2(im(z),re(z)); }
double complex csqrt(double complex z) {
    double x=re(z), y=im(z);
    if (isinf(y)) return zmake(INFINITY,y);
    if (isinf(x)) {
        if (x>0) return zmake(INFINITY, isnan(y) ? y : copysign(0,y));
        return zmake(isnan(y) ? y : 0, copysign(INFINITY,y));
    }
    if (isnan(x)||isnan(y)) return zmake(NAN,NAN);
    if (y==0) return x<0 ? zmake(0,copysign(sqrt(-x),y)) : zmake(sqrt(fabs(x)),y);
    /* Scaling avoids overflow in hypot and underflow in the half-sum. */
    int scale = 0;
    if (fabs(x)>DBL_MAX/4 || fabs(y)>DBL_MAX/4) { x*=0.25; y*=0.25; scale=1; }
    else if (fabs(x)<DBL_MIN && fabs(y)<DBL_MIN) { x*=0x1p54; y*=0x1p54; scale=-1; }
    double t=sqrt((mag(x,y)+fabs(x))*0.5);
    double a=x>=0?t:fabs(y)/(2*t), b=x>=0?y/(2*t):copysign(t,y);
    if (scale==1) { a*=2; b*=2; } else if (scale==-1) { a*=0x1p-27; b*=0x1p-27; }
    return zmake(a,b);
}
double complex cexp(double complex z) {
    double x=re(z), y=im(z);
    if (y==0) return zmake(exp(x),y);
    if (!isfinite(y)) {
        if (isinf(x) && x<0) return zmake(0,copysign(0,y));
        if (isinf(x) && x>0) return zmake(INFINITY,NAN);
        return zmake(NAN,NAN);
    }
    if (isinf(x)&&x<0) return zmake(copysign(0,cos(y)),copysign(0,sin(y)));
    return zmake(exp_product(x,cos(y)),exp_product(x,sin(y)));
}
double complex clog(double complex z) { return zmake(logmag(re(z),im(z)),atan2(im(z),re(z))); }
double complex cpow(double complex a,double complex b) { return cexp(mul(b,clog(a))); }
double complex csinh(double complex z) {
    double x=re(z),y=im(z);
    if (y==0) return zmake(sinh(x),y);
    if (!isfinite(y) && (x==0 || isinf(x))) {
        if (isinf(y)) feraiseexcept(FE_INVALID);
        return zmake(x, NAN);
    }
    if (isfinite(x) && fabs(x)>20) {
        double c=cos(y), si=sin(y), ax=fabs(x);
        return zmake(exp_product(ax,copysign(0.5,x)*c),exp_product(ax,0.5*si));
    }
    return zmake(sinh(x)*cos(y),cosh(x)*sin(y));
}
double complex ccosh(double complex z) {
    double x=re(z),y=im(z);
    if (y==0) return zmake(cosh(x),copysign(0,signbit(x) != signbit(y) ? -1.0 : 1.0));
    if (!isfinite(y) && (x==0 || isinf(x))) {
        if (isinf(y)) feraiseexcept(FE_INVALID);
        return zmake(x==0 ? NAN : INFINITY, x==0 ? copysign(0,x) : NAN);
    }
    if (isfinite(x) && fabs(x)>20) {
        double c=cos(y), si=sin(y), ax=fabs(x);
        return zmake(exp_product(ax,0.5*c),exp_product(ax,copysign(0.5,x)*si));
    }
    return zmake(cosh(x)*cos(y),sinh(x)*sin(y));
}
double complex csin(double complex z) { double complex t=csinh(zmake(-im(z),re(z))); return zmake(im(t),-re(t)); }
double complex ccos(double complex z) { return ccosh(zmake(-im(z),re(z))); }
double complex ctanh(double complex z) {
    double x=re(z),y=im(z);
    if (isinf(x) && !isfinite(y)) return zmake(copysign(1,x),copysign(0,y));
    if (!isfinite(y) && x==0) {
        if (isinf(y)) feraiseexcept(FE_INVALID);
        return zmake(x,NAN);
    }
    if (isnan(x) && y==0) return zmake(x,y);
    double sy=sin(y), cy=cos(y);
    double s2=(2*sy)*cy;
    if (isinf(x)) return zmake(copysign(1,x),copysign(0,s2));
    if (fabs(x)>20) {
        double v=fabs(x)>DBL_MAX/2 ? copysign(0,s2) : exp_product(-2*fabs(x),2*s2);
        return zmake(copysign(1,x),v);
    }
    double sh=sinh(x), ch=cosh(x), d=sh*sh+cy*cy;
    return zmake((sh*ch)/d,(sy*cy)/d);
}
double complex ctan(double complex z) { double complex t=ctanh(zmake(-im(z),re(z))); return zmake(im(t),-re(t)); }
/* Elliptic coordinates avoid forming z*z and thus avoid its overflow. */
double complex cacosh(double complex z) {
    const double pi=3.14159265358979323846264338327950288;
    const double ln2=0.693147180559945309417232121458176568;
    double x=re(z), y=im(z), ax=fabs(x), ay=fabs(y);
    if (isinf(y)) return zmake(INFINITY, isnan(x) ? NAN : copysign(isinf(x) ? (signbit(x)?3*pi/4:pi/4) : pi/2,y));
    if (isinf(x)) return zmake(INFINITY, isnan(y) ? NAN : copysign(signbit(x)?pi:0,y));
    if (isnan(x)||isnan(y)) return zmake(NAN,NAN);
    if (y==0) {
        if (x>=1) return zmake(acosh(x),y);
        if (x>=-1) return zmake(0,copysign(acos(x),y));
        return zmake(acosh(-x),copysign(pi,y));
    }
    if (ax>0x1p500 || ay>0x1p500) {
        double hi=ax>ay?ax:ay, lo=ax>ay?ay:ax, ratio=lo/hi;
        return zmake(log(hi)+0.5*log1p(ratio*ratio)+ln2,atan2(y,x));
    }
    double r=mag(ax+1,y), t=mag(ax-1,y), a=0.5*r+0.5*t;
    /* Form both a-1 and a-|x| without subtracting nearly equal values. */
    double d, delta;
    if (ax>=1) {
        double den1=r+ax+1, den2=t+(ax-1);
        d=0.5*ay*(ay/den1 + ay/den2);
        delta=(ax-1)+d;
    } else {
        delta=0.5*ay*(ay/(r+ax+1)+ay/(t+1-ax));
        d=(1-ax)+delta;
    }
    double real=log1p(delta+sqrt(delta)*sqrt(delta+2));
    double imag=atan2(sqrt(d)*sqrt(a+ax),x);
    if (ax>1 && ay<0x1p-26*(ax-1)) {
        double small=ay/(sqrt(ax-1)*sqrt(ax+1));
        imag=x>0 ? small : pi-small;
    }
    if (a==1 && ay!=0) {
        /* First order behavior away from the branch points. */
        if (ax<1) real=ay/sqrt((1-ax)*(1+ax));
        else if (ax>1) imag=atan2(ay/sqrt((ax-1)*(ax+1)), x<0?-1:1);
        else { double v=sqrt(ay); real=v; imag=x>0?v:pi-v; }
    }
    return zmake(real,copysign(imag,y));
}
double complex casinh(double complex z) {
    double x=re(z), y=im(z), ax=fabs(x), ay=fabs(y);
    if (y==0) return zmake(asinh(x),y);
    if (x==0 && ay<=1) return zmake(x,asin(y));
    if (ax<0x1p-27 && ay<0x1p-27) return z;
    if (isfinite(x) && isfinite(y) && (ax>0x1p500 || ay>0x1p500)) {
        double hi=ax>ay?ax:ay, lo=ax>ay?ay:ax, ratio=lo/hi;
        double real=log(hi)+0.5*log1p(ratio*ratio)+0.693147180559945309417232121458176568;
        if (x<0) real=-real;
        return zmake(real,atan2(y,ax));
    }
    if (!isfinite(x)||!isfinite(y)||ax>0x1p500||ay>0x1p500) {
        double complex t=cacosh(zmake(-y,x));
        return zmake(copysign(re(t),x),copysign(1.57079632679489661923,x)-im(t));
    }
    double r=mag(ax,ay+1), t=mag(ax,ay-1);
    double a=0.5*r+0.5*t, d, delta;
    if (ay>=1) {
        double u=r+ay+1, v=t+(ay-1);
        double part=0.5*ax*(ax/u+ax/v);
        delta=(ay-1)+part; d=part;
    } else {
        double u=r+1+ay, v=t+1-ay;
        double part=0.5*ax*(ax/u+ax/v);
        delta=part; d=(1-ay)+part;
    }
    double real=log1p(delta+sqrt(delta)*sqrt(delta+2));
    double angle=atan2(ay,sqrt(d)*sqrt(a+ay));
    if (delta==0 && ay<1) real=ax/sqrt((1-ay)*(1+ay));
    if (d==0 && ay>1 && ax!=0) angle=1.57079632679489661923-ax/(sqrt(ay-1)*sqrt(ay+1));
    return zmake(copysign(real,x),copysign(angle,y));
}
double complex casin(double complex z) {
    double complex t=casinh(zmake(-im(z),re(z)));
    return zmake(im(t),-re(t));
}
double complex cacos(double complex z) { double complex t=cacosh(z); return zmake(fabs(im(t)),-copysign(re(t),im(z))); }
double complex catanh(double complex z) {
    const double halfpi=1.57079632679489661923;
    double x=re(z), y=im(z), ax=fabs(x), ay=fabs(y);
    if (isinf(x)||isinf(y)) return zmake(copysign(0,x),copysign(halfpi,y));
    if (isnan(x)||isnan(y)) { if (x==0) return zmake(x,NAN); return zmake(NAN,NAN); }
    if (y==0 && ax<=1) return zmake(atanh(x),y);
    if (ax>0x1p500 || ay>0x1p500) {
        double scale=ax>ay?ax:ay, u=x/scale, v=y/scale;
        return zmake((u/(u*u+v*v))/scale,copysign(halfpi,y));
    }
    double d=(1-ax)*(1-ax)+y*y;
    double real;
    if (d<0x1p-500) real=0.5*(log(mag(1+ax,y))-log(mag(1-ax,y)));
    else real=0.25*log1p(4*ax/d);
    return zmake(copysign(real,x),0.5*atan2(2*y,(1-ax)*(1+ax)-y*y));
}
double complex catan(double complex z) { double complex t=catanh(zmake(-im(z),re(z))); return zmake(im(t),-re(t)); }

#define WRAP_REAL(name) \
float name##f(float complex z) { return (float)name(zmake(__real__ z,__imag__ z)); } \
long double name##l(long double complex z) { return (long double)name(zmake((double)__real__ z,(double)__imag__ z)); }
#define WRAP_COMPLEX(name) \
float complex name##f(float complex z) { double complex r=name(zmake(__real__ z,__imag__ z)); return CMPLXF(re(r),im(r)); } \
long double complex name##l(long double complex z) { double complex r=name(zmake((double)__real__ z,(double)__imag__ z)); return CMPLXL(re(r),im(r)); }
float crealf(float complex z) { return __real__ z; }
long double creall(long double complex z) { return __real__ z; }
float cimagf(float complex z) { return __imag__ z; }
long double cimagl(long double complex z) { return __imag__ z; }
WRAP_REAL(cabs) WRAP_REAL(carg)
float complex conjf(float complex z) { return CMPLXF(__real__ z,-__imag__ z); }
long double complex conjl(long double complex z) { return CMPLXL(__real__ z,-__imag__ z); }
float complex cprojf(float complex z) { return isinf(__real__ z)||isinf(__imag__ z) ? CMPLXF(INFINITY,copysignf(0,__imag__ z)) : z; }
long double complex cprojl(long double complex z) { return isinf(__real__ z)||isinf(__imag__ z) ? CMPLXL(INFINITY,copysignl(0,__imag__ z)) : z; } WRAP_COMPLEX(csqrt) WRAP_COMPLEX(cexp) WRAP_COMPLEX(clog)
WRAP_COMPLEX(csin) WRAP_COMPLEX(ccos) WRAP_COMPLEX(ctan) WRAP_COMPLEX(csinh) WRAP_COMPLEX(ccosh) WRAP_COMPLEX(ctanh)
WRAP_COMPLEX(casin) WRAP_COMPLEX(cacos) WRAP_COMPLEX(catan) WRAP_COMPLEX(casinh) WRAP_COMPLEX(cacosh) WRAP_COMPLEX(catanh)
float complex cpowf(float complex x,float complex y) { double complex r=cpow(zmake(__real__ x,__imag__ x),zmake(__real__ y,__imag__ y)); return CMPLXF(re(r),im(r)); }
long double complex cpowl(long double complex x,long double complex y) { double complex r=cpow(zmake((double)__real__ x,(double)__imag__ x),zmake((double)__real__ y,(double)__imag__ y)); return CMPLXL(re(r),im(r)); }
