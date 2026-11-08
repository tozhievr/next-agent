/* Narrow formatted input (C23 7.23.6). */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include "../internal/stdio_impl.h"
#include "fmt_core.h"

typedef struct scan_in {
    int (*get)(void *);
    int (*unget)(void *, int);
    void *ctx;
    size_t nread;
    int eof;
    int input_failure;
} scan_in;

static int file_get(void *p) { return __libc_getc_unlocked((FILE *)p); }
static int file_unget(void *p, int c) { return __libc_ungetc_unlocked(c, (FILE *)p); }
typedef struct { const unsigned char *s; size_t pos; } scan_str;
static int str_get(void *p) { scan_str *s=p; return s->s[s->pos] ? s->s[s->pos++] : EOF; }
static int str_unget(void *p, int c) { scan_str *s=p; if (!s->pos || s->s[s->pos-1] != (unsigned char)c) return EOF; --s->pos; return c; }
static int take(scan_in *in) { int c=in->get(in->ctx); if(c==EOF) in->eof=1; else in->nread++; return c; }
static void putback(scan_in *in,int c) { if(c!=EOF && in->unget(in->ctx,c)==c) { in->nread--; in->eof=0; } }
static int skip_space(scan_in *in) { int c; do { c=take(in); } while(c!=EOF && isspace((unsigned char)c)); putback(in,c); return c!=EOF; }

enum { LEN_DEF, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_CAPL, LEN_W, LEN_WF };
static int digit_value(int c) { if(c>='0'&&c<='9') return c-'0'; if(c>='a'&&c<='z') return c-'a'+10; if(c>='A'&&c<='Z') return c-'A'+10; return -1; }

/* Convert a byte-delimited narrow input item to wide characters after the
 * matching extent has been determined. NUL is a one-byte character here. */
static int scan_bytes_to_wide(const char *bytes,size_t n,wchar_t *out,size_t *count) {
    size_t i=0,kount=0;
    while(i<n) {
        uint32_t wc; size_t k;
        if((unsigned char)bytes[i]==0) { wc=0;k=1; }
        else k=fmt_mbdecode((const unsigned char *)bytes+i,n-i,&wc);
        if(k==(size_t)-1) return -1;
        if(k==(size_t)-2) return -2;
        if(!k) return -1;
        if(out) out[kount]=(wchar_t)wc;
        kount++;i+=k;
    }
    *count=kount;
    return 1;
}

/* Convert directly to the destination precision so decimal input receives
 * only one rounding step. */
static int scan_float_value(const char *s,int len,void *dst) {
    char *end;
    size_t n=strlen(s);
    if(len==LEN_CAPL) { long double v=strtold(s,&end); if(end!=s+n)return 0; if(dst)*(long double*)dst=v; }
    else if(len==LEN_L) { double v=strtod(s,&end); if(end!=s+n)return 0; if(dst)*(double*)dst=v; }
    else { float v=strtof(s,&end); if(end!=s+n)return 0; if(dst)*(float*)dst=v; }
    return 1;
}

static void scan_store_wide_int(void *p,int len,int bits,int is_signed,intmax_t sv,uintmax_t uv) {
    if(len==LEN_W) {
        switch(bits) { case 8: if(is_signed)*(int8_t*)p=(int8_t)sv;else*(uint8_t*)p=(uint8_t)uv;break; case 16: if(is_signed)*(int16_t*)p=(int16_t)sv;else*(uint16_t*)p=(uint16_t)uv;break; case 32: if(is_signed)*(int32_t*)p=(int32_t)sv;else*(uint32_t*)p=(uint32_t)uv;break; case 64: if(is_signed)*(int64_t*)p=(int64_t)sv;else*(uint64_t*)p=(uint64_t)uv;break; default: if(is_signed)*(intmax_t*)p=sv;else*(uintmax_t*)p=uv;break; }
    } else {
        switch(bits) { case 8: if(is_signed)*(int_fast8_t*)p=(int_fast8_t)sv;else*(uint_fast8_t*)p=(uint_fast8_t)uv;break; case 16: if(is_signed)*(int_fast16_t*)p=(int_fast16_t)sv;else*(uint_fast16_t*)p=(uint_fast16_t)uv;break; case 32: if(is_signed)*(int_fast32_t*)p=(int_fast32_t)sv;else*(uint_fast32_t*)p=(uint_fast32_t)uv;break; case 64: if(is_signed)*(int_fast64_t*)p=(int_fast64_t)sv;else*(uint_fast64_t*)p=(uint_fast64_t)uv;break; default: if(is_signed)*(intmax_t*)p=sv;else*(uintmax_t*)p=uv;break; }
    }
}

static int scan_buf_push(char **buf,size_t *cap,char *local,size_t *n,int c) {
    if(*n+1>=*cap) {
        if(*cap>SIZE_MAX/2)return 0;
        size_t next=*cap*2; char *p;
        if(*buf==local) { p=malloc(next); if(!p)return 0; memcpy(p,local,*n); }
        else { p=realloc(*buf,next); if(!p)return 0; }
        *buf=p;*cap=next;
    }
    (*buf)[(*n)++]=(char)c;return 1;
}

static int scan_core(scan_in *in,const char *fmt,va_list ap) {
    int assigned=0, matched=0;
#define SCAN_PUSH(ch) do { int scan_ch_=(ch); if(!scan_buf_push(&buf,&capacity,local,&n,scan_ch_)){putback(in,scan_ch_);if(buf!=local)free(buf);errno=ENOMEM;return assigned;} } while(0)
    while(*fmt) {
        if(isspace((unsigned char)*fmt)) { while(isspace((unsigned char)*fmt)) fmt++; skip_space(in); continue; }
        if(*fmt!='%') { int c=take(in); if(c!=*fmt) { putback(in,c); break; } fmt++; continue; }
        fmt++; if(*fmt=='%') { skip_space(in); int c=take(in); if(c!='%') { putback(in,c); break; } fmt++; continue; }
        int suppress=0; if(*fmt=='*') { suppress=1; fmt++; }
        size_t width=0; while(isdigit((unsigned char)*fmt)) { unsigned d=(unsigned)(*fmt-'0'); if(width>(SIZE_MAX-d)/10) width=SIZE_MAX; else if(width!=SIZE_MAX) width=width*10+d; fmt++; }
        if(width==0) width=(size_t)-1;
        int len=LEN_DEF, intbits=0;
        if(fmt[0]=='h'&&fmt[1]=='h') { len=LEN_HH; fmt+=2; } else if(*fmt=='h') {len=LEN_H;fmt++;}
        else if(fmt[0]=='l'&&fmt[1]=='l') {len=LEN_LL;fmt+=2;} else if(*fmt=='l') {len=LEN_L;fmt++;}
        else if(*fmt=='j') {len=LEN_J;fmt++;} else if(*fmt=='z') {len=LEN_Z;fmt++;} else if(*fmt=='t') {len=LEN_T;fmt++;} else if(*fmt=='L') {len=LEN_CAPL;fmt++;}
        else if(*fmt=='w') { len=LEN_W;fmt++;if(*fmt=='f'){len=LEN_WF;fmt++;}while(isdigit((unsigned char)*fmt)){if(intbits<1000)intbits=intbits*10+(*fmt-'0');fmt++;} }
        if(!*fmt) break;
        int conv=(unsigned char)*fmt++;
        if(conv=='n') { if(!suppress) { void *p=va_arg(ap,void*); switch(len) {case LEN_HH:*(signed char*)p=(signed char)in->nread;break;case LEN_H:*(short*)p=(short)in->nread;break;case LEN_L:*(long*)p=(long)in->nread;break;case LEN_LL:*(long long*)p=(long long)in->nread;break;case LEN_J:*(intmax_t*)p=(intmax_t)in->nread;break;case LEN_Z:*(ptrdiff_t*)p=(ptrdiff_t)in->nread;break;case LEN_T:*(ptrdiff_t*)p=(ptrdiff_t)in->nread;break;case LEN_W:case LEN_WF:scan_store_wide_int(p,len,intbits,1,(intmax_t)in->nread,0);break;default:*(int*)p=(int)in->nread;} } matched++; continue; }
        if(conv!='c' && conv!='[' && conv!='%' && conv!='C' && conv!='S') skip_space(in);
        if(conv=='c'||conv=='s'||conv=='[') {
            if(len==LEN_L) {
                unsigned char set[256]={0}; int invert=0;
                if(conv=='[') {
                    if(*fmt=='^'){invert=1;fmt++;}
                    if(*fmt==']'){set[(unsigned char)']']=1;fmt++;}
                    while(*fmt&&*fmt!=']') { unsigned char a=(unsigned char)*fmt++; if(*fmt=='-'&&fmt[1]&&fmt[1]!=']'){fmt++;unsigned char b=(unsigned char)*fmt++;for(unsigned x=a;x<=b;x++)set[x]=1;}else set[a]=1; }
                    if(*fmt==']')fmt++;else break;
                }
                wchar_t *out=suppress?NULL:va_arg(ap,wchar_t*); char local_mb[128],*mb=local_mb; size_t cap=sizeof local_mb,nbyte=0,count=0,lim=conv=='c'&&width==(size_t)-1?1:width; int c;
                while(nbyte<lim) {
                    c=take(in);if(c==EOF)break;
                    if(conv=='s'&&isspace((unsigned char)c)){putback(in,c);break;}
                    if(conv=='['&&(set[(unsigned char)c]^invert)==0){putback(in,c);break;}
                    if(!scan_buf_push(&mb,&cap,local_mb,&nbyte,c)){putback(in,c);if(mb!=local_mb)free(mb);errno=ENOMEM;return assigned;}
                    if(conv=='c'&&width==(size_t)-1)break;
                }
                if(!nbyte){if(mb!=local_mb)free(mb);break;}
                int decoded=scan_bytes_to_wide(mb,nbyte,out,&count);
                if(decoded<0){if(decoded==-1||(decoded==-2&&in->eof)){errno=EILSEQ;in->input_failure=1;}if(mb!=local_mb)free(mb);break;}
                if(conv=='c'&&nbyte<lim){in->eof=0;if(mb!=local_mb)free(mb);break;}
                if(!count){if(mb!=local_mb)free(mb);break;}
                if(out&&conv!='c')out[count]=0;
                if(mb!=local_mb)free(mb);matched++;if(!suppress)assigned++;continue;
            }
            unsigned char set[256]={0}; int invert=0;
            if(conv=='[') { if(*fmt=='^') {invert=1;fmt++;} if(*fmt==']') {set[(unsigned char)']']=1;fmt++;} while(*fmt&&*fmt!=']') { unsigned char a=(unsigned char)*fmt++; if(*fmt=='-'&&fmt[1]&&fmt[1]!=']') {fmt++;unsigned char b=(unsigned char)*fmt++;for(unsigned x=a;x<=b;x++)set[x]=1;} else set[a]=1; } if(*fmt==']')fmt++;else break; }
            unsigned char *out=suppress?NULL:va_arg(ap,unsigned char*); size_t n=0,lim=conv=='c'&&width==(size_t)-1?1:width; int c;
            while(n<lim) { c=take(in); if(c==EOF) break; if(conv=='s'&&isspace((unsigned char)c)) {putback(in,c);break;} if(conv=='[' && (set[(unsigned char)c]^invert)==0) {putback(in,c);break;} if(out)out[n]=(unsigned char)c; n++; if(conv=='c'&&width==(size_t)-1)break; }
            if(!n || (conv=='c'&&n<lim)) { if(conv=='c'&&n&&!in->input_failure) in->eof=0; if(out&&conv!='c') out[0]=0; break; }
            if(out&&conv!='c')out[n]=0; matched++; if(!suppress)assigned++; continue;
        }
        if(conv=='d'||conv=='i'||conv=='u'||conv=='o'||conv=='x'||conv=='X'||conv=='b'||conv=='B'||conv=='p') {
            char local[1024],*buf=local; size_t capacity=sizeof(local),n=0,lim=width; int c;
            int base=conv=='o'?8:(conv=='x'||conv=='X'||conv=='p'?16:(conv=='b'||conv=='B')?2:conv=='i'?0:10);
            int first;
            if(n<lim) { c=take(in); if(c=='+'||c=='-') SCAN_PUSH(c); else putback(in,c); }
            first=n<lim?take(in):EOF;
            if(first!=EOF && digit_value(first)>=0 && (base==0 || digit_value(first)<base)) {
                SCAN_PUSH(first);
                int actual=base;
                if(base==0) actual=first=='0'?8:10;
                if(first=='0' && (base==0||base==16||base==2) && n<lim) {
                    c=take(in);
                    int is_hex=(c=='x'||c=='X'),is_bin=(c=='b'||c=='B');
                    if((is_hex&&(base==0||base==16))||(is_bin&&(base==0||base==2))) {
                        int prefbase=is_hex?16:2;
                        SCAN_PUSH(is_hex?'x':'b');actual=prefbase;
                        if(n<lim) { int d=take(in);if(d!=EOF&&digit_value(d)>=0&&digit_value(d)<prefbase)SCAN_PUSH(d);else putback(in,d); }
                    } else putback(in,c);
                }
                while(n<lim) { if(!scan_buf_push(&buf,&capacity,local,&n,0)){if(buf!=local)free(buf);errno=ENOMEM;return assigned;} n--; c=take(in); if(c==EOF)break; int d=digit_value(c); if(d>=0&&d<actual)SCAN_PUSH(c); else {putback(in,c);break;} }
            } else { putback(in,first); if(n)in->eof=0; if(buf!=local)free(buf); break; }
            buf[n]=0; char *end; int signed_conv=conv=='d'||conv=='i'; long long sv=signed_conv?strtoll(buf,&end,base):0; unsigned long long u=signed_conv?0:strtoull(buf,&end,base);
            if((size_t)(end-buf)!=n) {if(n)in->eof=0;if(buf!=local)free(buf);break;}
            if(!suppress) { void *p=va_arg(ap,void*); if(conv=='p')*(void**)p=(void*)(uintptr_t)u; else if(len==LEN_W||len==LEN_WF)scan_store_wide_int(p,len,intbits,signed_conv,(intmax_t)sv,(uintmax_t)u); else if(signed_conv) { switch(len){case LEN_HH:*(signed char*)p=(signed char)sv;break;case LEN_H:*(short*)p=(short)sv;break;case LEN_L:*(long*)p=(long)sv;break;case LEN_LL:*(long long*)p=sv;break;case LEN_J:*(intmax_t*)p=(intmax_t)sv;break;case LEN_Z:*(ptrdiff_t*)p=(ptrdiff_t)sv;break;case LEN_T:*(ptrdiff_t*)p=(ptrdiff_t)sv;break;default:*(int*)p=(int)sv;} } else { switch(len){case LEN_HH:*(unsigned char*)p=(unsigned char)u;break;case LEN_H:*(unsigned short*)p=(unsigned short)u;break;case LEN_L:*(unsigned long*)p=(unsigned long)u;break;case LEN_LL:*(unsigned long long*)p=u;break;case LEN_J:*(uintmax_t*)p=(uintmax_t)u;break;case LEN_Z:*(size_t*)p=(size_t)u;break;case LEN_T:*(uintptr_t*)p=(uintptr_t)u;break;default:*(unsigned int*)p=(unsigned int)u;} } assigned++; } if(buf!=local)free(buf); matched++; continue;
        }
        if(strchr("aAeEfFgG",conv)) {
            char local[1024],*buf=local;size_t capacity=sizeof(local),n=0,lim=width;int c,digits=0;
            if(n<lim) { c=take(in); if(c=='+'||c=='-') SCAN_PUSH(c); else putback(in,c); }
            /* inf/infinity and nan are words; recognize only complete tokens. */
            if(n<lim) {
                int c1=take(in);
                if(c1=='i'||c1=='I'||c1=='n'||c1=='N') {
                    int isinf=(c1=='i'||c1=='I');
                    const char *tail=isinf?"nf":"an"; char matched_tail[2]; size_t got=0;
                    while(got<2 && n+1+got<lim) { c=take(in); if(tolower((unsigned char)c)==tail[got]) {matched_tail[got++]=(char)c;} else {putback(in,c);break;} }
                    if(got==2) {
                        SCAN_PUSH(c1);SCAN_PUSH(tail[0]);SCAN_PUSH(tail[1]);
                        if(isinf) {
                            const char *suf="inity";size_t k=0;
                            while(k<5&&n<lim) { c=take(in);if(tolower((unsigned char)c)==suf[k]){SCAN_PUSH(c);k++;}else{putback(in,c);break;} }
                        } else if(n<lim) {
                            c=take(in);
                            if(c=='(') {
                                SCAN_PUSH(c);
                                while(n<lim) { c=take(in);if(isalnum((unsigned char)c)||c=='_')SCAN_PUSH(c);else if(c==')'){SCAN_PUSH(c);break;}else{putback(in,c);break;} }
                            } else putback(in,c);
                        }
                        buf[n]=0; void *dst=suppress?NULL:va_arg(ap,void*);
                        if(scan_float_value(buf,len,dst)){if(dst)assigned++;if(buf!=local)free(buf);matched++;continue;}
                        if(buf!=local)free(buf);break;
                    } else {
                        while(got) putback(in,(unsigned char)matched_tail[--got]);
                        putback(in,c1);
                    }
                } else putback(in,c1);
            }
            /* Hexadecimal significand and optional binary exponent. */
            if(n<lim) {
                int h0=take(in), hx=EOF;
                if(h0=='0' && n+1<lim) hx=take(in);
                if(h0=='0' && (hx=='x'||hx=='X')) {
                    SCAN_PUSH('0');SCAN_PUSH('x');size_t mant=0;
                    while(n<lim){c=take(in);if(digit_value(c)>=0&&digit_value(c)<16){SCAN_PUSH(c);mant++;}else{putback(in,c);break;}}
                    if(n<lim){c=take(in);if(c=='.'){SCAN_PUSH(c);while(n<lim){c=take(in);if(digit_value(c)>=0&&digit_value(c)<16){SCAN_PUSH(c);mant++;}else{putback(in,c);break;}}}else putback(in,c);}
                    if(mant && n<lim){c=take(in);if(c=='p'||c=='P'){SCAN_PUSH(c);if(n<lim){c=take(in);if(c=='+'||c=='-')SCAN_PUSH(c);else putback(in,c);}int d=n<lim?take(in):EOF;if(d>='0'&&d<='9'){SCAN_PUSH(d);while(n<lim){c=take(in);if(c>='0'&&c<='9')SCAN_PUSH(c);else{putback(in,c);break;}}}else putback(in,d);}else putback(in,c);}
                    buf[n]=0;void*dst=suppress?NULL:va_arg(ap,void*);if(!scan_float_value(buf,len,dst)){if(n)in->eof=0;if(buf!=local)free(buf);break;}if(dst)assigned++;if(buf!=local)free(buf);matched++;continue;
                } else { putback(in,hx);putback(in,h0); }
            }
            while(n<lim) { c=take(in); if(c>='0'&&c<='9') {SCAN_PUSH(c);digits++;} else {putback(in,c);break;} }
            if(n<lim) { c=take(in); if(c=='.') {SCAN_PUSH(c);while(n<lim){c=take(in);if(c>='0'&&c<='9'){SCAN_PUSH(c);digits++;}else{putback(in,c);break;}} } else putback(in,c); }
            if(!digits) { if(n)in->eof=0;if(buf!=local)free(buf); break; }
            if(n<lim) { c=take(in); if(c=='e'||c=='E') { SCAN_PUSH(c);if(n<lim){c=take(in);if(c=='+'||c=='-')SCAN_PUSH(c);else putback(in,c);} int d=n<lim?take(in):EOF;if(d>='0'&&d<='9'){SCAN_PUSH(d);while(n<lim){c=take(in);if(c>='0'&&c<='9')SCAN_PUSH(c);else{putback(in,c);break;}}}else putback(in,d); } else putback(in,c); }
            buf[n]=0;void*dst=suppress?NULL:va_arg(ap,void*);if(!scan_float_value(buf,len,dst)){if(n)in->eof=0;if(buf!=local)free(buf);break;}if(dst)assigned++;if(buf!=local)free(buf);matched++;continue;
        }
        break;
    }
    if(!matched && (in->eof||in->input_failure)) return EOF;
    return assigned;
}
#undef SCAN_PUSH

int vfscanf(FILE *restrict f,const char *restrict fmt,__gnuc_va_list ap) { if(!f||!fmt)return EOF; __libc_flockfile(f); __libc_fwide_unlocked(f,-1); scan_in in={.get=file_get,.unget=file_unget,.ctx=f}; int r=scan_core(&in,fmt,ap); __libc_funlockfile(f); return r; }
int fscanf(FILE *restrict f,const char *restrict fmt,...) { va_list ap; va_start(ap,fmt); int r=vfscanf(f,fmt,ap); va_end(ap); return r; }
int vscanf(const char *restrict fmt,__gnuc_va_list ap) { return vfscanf(stdin,fmt,ap); }
int scanf(const char *restrict fmt,...) { va_list ap; va_start(ap,fmt); int r=vscanf(fmt,ap); va_end(ap); return r; }
int vsscanf(const char *restrict s,const char *restrict fmt,__gnuc_va_list ap) { if(!s||!fmt)return EOF; scan_str str={(const unsigned char*)s,0}; scan_in in={.get=str_get,.unget=str_unget,.ctx=&str}; return scan_core(&in,fmt,ap); }
int sscanf(const char *restrict s,const char *restrict fmt,...) { va_list ap; va_start(ap,fmt); int r=vsscanf(s,fmt,ap); va_end(ap); return r; }
