/* Exact binary64 to decimal formatting. The coefficient is at most 767 digits:
 * 53-bit significand times 5^1074. Extra requested digits are emitted as zeros. */
#include "fmt_core.h"
#include <fenv.h>
#include <string.h>

void __libc_fmt_decode_double(double v,struct fmt_fp *f){union{double d;uint64_t u;}x={v};unsigned e=(unsigned)((x.u>>52)&2047);uint64_t m=x.u&0xfffffffffffffULL;f->sign=(int)(x.u>>63);f->sub=!e;f->emin=-1022;f->cls=e==2047?(m?3:2):(!e&&!m?0:1);f->mant=e&&e!=2047?m|(1ULL<<52):m;f->e2=e?(int)e-1023-52:-1074;}
void __libc_fmt_decode_ldouble(long double v,struct fmt_fp *f){__libc_fmt_decode_double((double)v,f);}
int __libc_fmt_round_mode(void){int r=fegetround();return r==FE_UPWARD?1:r==FE_DOWNWARD?2:r==FE_TOWARDZERO?3:0;}
static int round_up(int sign,int first,int rest,int odd){int mode=__libc_fmt_round_mode();if(!(first||rest))return 0;if(mode==1)return !sign;if(mode==2)return sign;if(mode==3)return 0;return first>5||(first==5&&(rest||odd));}
struct dec {char d[1100];int n;int exp;};
/* Base 10^9 limbs reduce the work needed for binary subnormals while
 * retaining the exact decimal coefficient. */
static void decimal(struct dec *x,const struct fmt_fp *f){
 uint32_t limb[128];int count=0;uint64_t m=(uint64_t)f->mant;
 do{limb[count++]=(uint32_t)(m%1000000000ULL);m/=1000000000ULL;}while(m);
 unsigned factor=f->e2<0?5u:2u;int steps=f->e2<0?-f->e2:f->e2;
 for(int i=0;i<steps;i++){uint64_t carry=0;for(int j=0;j<count;j++){uint64_t v=(uint64_t)limb[j]*factor+carry;limb[j]=(uint32_t)(v%1000000000ULL);carry=v/1000000000ULL;}if(carry)limb[count++]=(uint32_t)carry;}
 char tmp[10];int n=0;uint32_t top=limb[--count];do{tmp[n++]=(char)('0'+top%10);top/=10;}while(top);
 int pos=0;while(n)x->d[pos++]=tmp[--n];
 while(count){uint32_t v=limb[--count];for(int j=8;j>=0;j--){tmp[j]=(char)('0'+v%10);v/=10;}for(int j=0;j<9;j++)x->d[pos++]=tmp[j];}
 x->n=pos;x->exp=pos-1-(f->e2<0?-f->e2:0);
}
/* Round to k significant positions. k may be zero or negative. */
static void round_dec(struct dec *x,int64_t k,int sign){if(k>=x->n)return;int first=0,rest=0,odd=0;if(k>=0){first=x->d[k]-'0';for(int i=(int)k+1;i<x->n;i++)if(x->d[i]!='0'){rest=1;break;}if(k>0)odd=(x->d[k-1]-'0')&1;}else{for(int i=0;i<x->n;i++)if(x->d[i]!='0'){rest=1;break;}}
 int up=round_up(sign,first,rest,odd);if(k<=0){int old=x->exp;x->n=1;x->d[0]=up?'1':'0';x->exp=up?(int)((int64_t)old-k+1):0;return;}x->n=(int)k;if(up){int i=x->n-1;while(i>=0&&x->d[i]=='9')x->d[i--]='0';if(i>=0)x->d[i]++;else{x->d[0]='1';for(int j=1;j<x->n;j++)x->d[j]='0';x->exp++;}}}
static void emit_digits(const fmt_emitter *e,const struct dec *x,int64_t start,size_t n){if(start<0){size_t z=(uint64_t)(-start)>n?n:(size_t)-start;e->fill(e->ctx,'0',z);n-=z;start+=(int64_t)z;}if(n&&start<x->n){size_t k=(size_t)(x->n-start);if(k>n)k=n;e->puts(e->ctx,x->d+(size_t)start,k);n-=k;}if(n)e->fill(e->ctx,'0',n);}
static size_t exponent(char *buf,int exp,int upper,char marker){size_t n=0;buf[n++]=upper?(char)(marker-'a'+'A'):marker;buf[n++]=exp<0?'-':'+';unsigned v=exp<0?(unsigned)-exp:(unsigned)exp;char a[16];int k=0;do{a[k++]=(char)('0'+v%10);v/=10;}while(v);if(marker=='e')while(k<2)a[k++]='0';while(k)buf[n++]=a[--k];return n;}
static void padding(const fmt_emitter *e,unsigned flags,int width,size_t size,char sign,int special){size_t p=width>0&&(size_t)width>size?(size_t)width-size:0;if(!(flags&FMT_LEFT)&&(!(flags&FMT_ZERO)||special))e->fill(e->ctx,' ',p);if(sign)e->puts(e->ctx,&sign,1);if(!(flags&FMT_LEFT)&&(flags&FMT_ZERO)&&!special)e->fill(e->ctx,'0',p);}
static void tail(const fmt_emitter *e,unsigned flags,int width,size_t size){if((flags&FMT_LEFT)&&width>0&&(size_t)width>size)e->fill(e->ctx,' ',(size_t)width-size);}
static void hex(const fmt_emitter *e,unsigned flags,int width,int prec,int upper,const struct fmt_fp *f,char sign){uint64_t sig=(uint64_t)f->mant;int ex=f->cls?f->e2+52:0;int p=prec<0?13:prec;if(p<13){int shift=52-4*p;uint64_t rem=sig&((1ULL<<shift)-1);int first=(int)(rem>>(shift-1));int rest=(rem&((1ULL<<(shift-1))-1))!=0;uint64_t q=sig>>shift;if(round_up(f->sign,first?5:0,rest,(int)(q&1)))q++;sig=q<<shift;}if(prec<0)while(p>0&&!(sig&((uint64_t)15<<(52-4*p))))p--;char eb[16];size_t en=exponent(eb,ex,upper,'p');size_t size=(sign!=0)+2+1+(p||flags&FMT_ALT?1:0)+(size_t)p+en;size_t pad=width>0&&(size_t)width>size?(size_t)width-size:0;if(!(flags&FMT_LEFT)&&!(flags&FMT_ZERO))e->fill(e->ctx,' ',pad);if(sign)e->puts(e->ctx,&sign,1);e->puts(e->ctx,upper?"0X":"0x",2);if(!(flags&FMT_LEFT)&&(flags&FMT_ZERO))e->fill(e->ctx,'0',pad);const char *digits=upper?"0123456789ABCDEF":"0123456789abcdef";char c=digits[(sig>>52)&15];e->puts(e->ctx,&c,1);if(p||flags&FMT_ALT)e->puts(e->ctx,".",1);int k=p<13?p:13;char b[13];for(int i=0;i<k;i++)b[i]=digits[(sig>>(48-4*i))&15];e->puts(e->ctx,b,k);if(p>k)e->fill(e->ctx,'0',(size_t)(p-k));e->puts(e->ctx,eb,en);tail(e,flags,width,size);}
void __libc_fmt_float(const fmt_emitter *e,unsigned flags,int width,int prec,int conv,const struct fmt_fp *f){int upper=conv>='A'&&conv<='Z';int c=upper?conv-'A'+'a':conv;char sign=f->sign?'-':(flags&FMT_PLUS)?'+':(flags&FMT_SPACE)?' ':0;if(f->cls>=2){const char *s=f->cls==2?(upper?"INF":"inf"):(upper?"NAN":"nan");size_t size=3+(sign!=0);padding(e,flags,width,size,sign,1);e->puts(e->ctx,s,3);tail(e,flags,width,size);return;}if(c=='a'){hex(e,flags,width,prec,upper,f,sign);return;}
 struct dec x;if(f->cls)decimal(&x,f);else{x.d[0]='0';x.n=1;x.exp=0;}int p=prec<0?6:prec;if(c=='g'&&p==0)p=1;int64_t keep=c=='f'?(int64_t)x.exp+1+p:c=='e'?(int64_t)p+1:p;round_dec(&x,keep,f->sign);
 int sci=c=='e'||(c=='g'&&(x.exp< -4||x.exp>=p));int64_t frac=c=='g'?(sci?(int64_t)p-1:(int64_t)p-1-x.exp):p;if(frac<0)frac=0;
 if(c=='g'&&!(flags&FMT_ALT)){int last=x.n-1;while(last>0&&x.d[last]=='0')last--;int64_t used=sci?last:(int64_t)last-x.exp;if(used<0)used=0;if(frac>used)frac=used;}
 size_t intlen=sci?1:(x.exp>=0?(size_t)x.exp+1:1);char eb[16];size_t en=sci?exponent(eb,x.exp,upper,'e'):0;size_t size=(sign!=0)+intlen+(frac||flags&FMT_ALT?1:0)+(size_t)frac+en;padding(e,flags,width,size,sign,0);
 if(sci)emit_digits(e,&x,0,1);else if(x.exp<0)e->puts(e->ctx,"0",1);else emit_digits(e,&x,0,intlen);if(frac||flags&FMT_ALT)e->puts(e->ctx,".",1);if(frac)emit_digits(e,&x,sci?1:(int64_t)x.exp+1,(size_t)frac);if(sci)e->puts(e->ctx,eb,en);tail(e,flags,width,size);
}

