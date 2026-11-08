/* Wide formatted input (C23 7.31.2). */
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "../internal/stdio_impl.h"

typedef struct { wint_t (*get)(void *); wint_t (*unget)(void *, wint_t); void *ctx; size_t count; int eof; int encoding_error; } wscan_in;
static wint_t wscan_file_get(void *p) { return __libc_getwc_unlocked((FILE *)p); }
static wint_t wscan_file_unget(void *p, wint_t c) { return __libc_ungetwc_unlocked(c, (FILE *)p); }
typedef struct { const wchar_t *s; size_t pos; } wscan_str;
static wint_t wscan_str_get(void *p) { wscan_str *s=p; return s->s[s->pos] ? (wint_t)s->s[s->pos++] : WEOF; }
static wint_t wscan_str_unget(void *p, wint_t c) { wscan_str *s=p; if (!s->pos || s->s[s->pos-1] != (wchar_t)c) return WEOF; --s->pos; return c; }
static wint_t wscan_take(wscan_in *in) { int saved=errno;errno=0;wint_t c=in->get(in->ctx);int e=errno;if(c==WEOF)in->eof=1;else in->count++;if(c==WEOF&&e==EILSEQ)in->encoding_error=1;if(!e)errno=saved;return c; }
static void wscan_back(wscan_in *in,wint_t c) { if(c!=WEOF&&in->unget(in->ctx,c)==c){in->count--;in->eof=0;} }
static int wscan_space(wscan_in *in) { wint_t c; do { c=wscan_take(in); } while(c!=WEOF&&iswspace(c)); wscan_back(in,c); return c!=WEOF; }
enum { WLEN_DEF, WLEN_HH, WLEN_H, WLEN_L, WLEN_LL, WLEN_J, WLEN_Z, WLEN_T, WLEN_CAPL, WLEN_W, WLEN_WF };

static int wscan_set_member(wchar_t c,const wchar_t *p,const wchar_t *end,int invert) {
    int found=0;
    while(p<end){wchar_t a=*p++;if(p+1<end&&*p==L'-'){p++;wchar_t b=*p++;if((a<=b&&c>=a&&c<=b)||(a>b&&c<=a&&c>=b))found=1;}else if(c==a)found=1;}
    return found!=invert;
}
typedef struct { wchar_t *p; size_t n,cap; int error; } wscan_buf;
static int wscan_append(wscan_buf *b,wchar_t c){if(b->error)return 0;if(b->n+1>=b->cap){size_t cap=b->cap?b->cap*2:32;if(cap<b->cap||cap>SIZE_MAX/sizeof *b->p){errno=ENOMEM;b->error=1;return 0;}wchar_t*p=realloc(b->p,cap*sizeof *p);if(!p){errno=ENOMEM;b->error=1;return 0;}b->p=p;b->cap=cap;}b->p[b->n++]=c;return 1;}
static void wscan_buf_free(wscan_buf *b){free(b->p);b->p=NULL;b->n=b->cap=0;}
static int wscan_digit(wint_t c,int base){int d=(c>=L'0'&&c<=L'9')?(int)(c-L'0'):(c>=L'a'&&c<=L'z')?(int)(c-L'a'+10):(c>=L'A'&&c<=L'Z')?(int)(c-L'A'+10):-1;return d>=0&&d<base;}
static wint_t wscan_float(wscan_in *in,size_t width,wscan_buf *b){
    wint_t c=width?wscan_take(in):WEOF;
    if(c==L'+'||c==L'-'){if(!wscan_append(b,(wchar_t)c))return WEOF;width--;}
    else wscan_back(in,c);
    if(!width)return WEOF;
    c=wscan_take(in);
    if(c==L'i'||c==L'I'||c==L'n'||c==L'N'){
        int inf=c==L'i'||c==L'I';wchar_t tail[2]={inf?L'n':L'a',inf?L'f':L'n'};
        if(!wscan_append(b,(wchar_t)c))return WEOF;width--;
        for(int i=0;i<2;i++){if(!width)return WEOF;wint_t q=wscan_take(in);wchar_t lo=(q>=L'A'&&q<=L'Z')?(wchar_t)(q+32):(wchar_t)q;if(lo!=tail[i]){wscan_back(in,q);return WEOF;}if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}
        if(!inf&&width){wint_t q=wscan_take(in);if(q==L'('){if(!wscan_append(b,L'('))return WEOF;width--;while(width){q=wscan_take(in);if((q>=L'0'&&q<=L'9')||(q>=L'a'&&q<=L'z')||(q>=L'A'&&q<=L'Z')||q==L'_'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else if(q==L')'){if(!wscan_append(b,L')'))return WEOF;width--;break;}else{wscan_back(in,q);break;}}}else wscan_back(in,q);}
        if(inf&&width){wchar_t suffix[]=L"inity";for(int i=0;i<5&&width;i++){wint_t q=wscan_take(in);wchar_t lo=(q>=L'A'&&q<=L'Z')?(wchar_t)(q+32):(wchar_t)q;if(lo!=suffix[i]){wscan_back(in,q);break;}if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}}
        return c;
    }
    if(c==L'0'){
        if(!wscan_append(b,L'0'))return WEOF;width--;
        if(width){wint_t x=wscan_take(in);if(x==L'x'||x==L'X'){
            if(!wscan_append(b,L'x'))return WEOF;width--;int digits=0;
            while(width){wint_t q=wscan_take(in);if(wscan_digit(q,16)){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;digits++;}else{wscan_back(in,q);break;}}
            if(width){wint_t q=wscan_take(in);if(q==L'.'){if(!wscan_append(b,L'.'))return WEOF;width--;while(width){q=wscan_take(in);if(wscan_digit(q,16)){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;digits++;}else{wscan_back(in,q);break;}}}else wscan_back(in,q);}
            if(width){wint_t q=wscan_take(in);if(q==L'p'||q==L'P'){if(!wscan_append(b,L'p'))return WEOF;width--;if(width){q=wscan_take(in);if(q==L'+'||q==L'-'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else wscan_back(in,q);}while(width){q=wscan_take(in);if(q>=L'0'&&q<=L'9'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else{wscan_back(in,q);break;}}}else wscan_back(in,q);}
            (void)digits;return c;
        }wscan_back(in,x);}
    }else if(c>=L'1'&&c<=L'9'){if(!wscan_append(b,(wchar_t)c))return WEOF;width--;}
    else if(c==L'.'){if(!wscan_append(b,L'.'))return WEOF;width--;}
    else {wscan_back(in,c);return WEOF;}
    while(width){wint_t q=wscan_take(in);if(q>=L'0'&&q<=L'9'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else{wscan_back(in,q);break;}}
    if(width){wint_t q=wscan_take(in);if(q==L'.'){if(!wscan_append(b,L'.'))return WEOF;width--;while(width){q=wscan_take(in);if(q>=L'0'&&q<=L'9'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else{wscan_back(in,q);break;}}}else wscan_back(in,q);}
    if(width){wint_t q=wscan_take(in);if(q==L'e'||q==L'E'){if(!wscan_append(b,L'e'))return WEOF;width--;if(width){q=wscan_take(in);if(q==L'+'||q==L'-'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else wscan_back(in,q);}while(width){q=wscan_take(in);if(q>=L'0'&&q<=L'9'){if(!wscan_append(b,(wchar_t)q))return WEOF;width--;}else{wscan_back(in,q);break;}}}else wscan_back(in,q);}
    return c;
}

static int wscan_finish(wscan_in *in,int assigned,int matched) { return !matched&&(in->eof||in->encoding_error)?EOF:assigned; }

static int wscan_core(wscan_in *in,const wchar_t *fmt,va_list ap) {
    int assigned=0,matched=0;
    while(*fmt) {
        if(iswspace(*fmt)){while(iswspace(*fmt))fmt++;wscan_space(in);continue;}
        if(*fmt!=L'%'){wint_t c=wscan_take(in);if(c!=(wint_t)*fmt){wscan_back(in,c);break;}fmt++;continue;}
        fmt++; if(*fmt==L'%'){wscan_space(in);wint_t c=wscan_take(in);if(c!=L'%'){wscan_back(in,c);break;}fmt++;continue;}
        int suppress=0;if(*fmt==L'*'){suppress=1;fmt++;}
        size_t width=0;while(*fmt>=L'0'&&*fmt<=L'9'){size_t d=(size_t)(*fmt-L'0');if(width<=(SIZE_MAX-d)/10)width=width*10+d;else width=SIZE_MAX;fmt++;}if(!width)width=SIZE_MAX;
        int len=WLEN_DEF,lenbits=0;
        if(*fmt==L'w'){fmt++;if(*fmt==L'f'){len=WLEN_WF;fmt++;}else len=WLEN_W;while(*fmt>=L'0'&&*fmt<=L'9'){if(lenbits<10000)lenbits=lenbits*10+(int)(*fmt-L'0');fmt++;}}
        else if(fmt[0]==L'h'&&fmt[1]==L'h'){len=WLEN_HH;fmt+=2;}else if(*fmt==L'h'){len=WLEN_H;fmt++;}
        else if(fmt[0]==L'l'&&fmt[1]==L'l'){len=WLEN_LL;fmt+=2;}else if(*fmt==L'l'){len=WLEN_L;fmt++;}
        else if(*fmt==L'j'){len=WLEN_J;fmt++;}else if(*fmt==L'z'){len=WLEN_Z;fmt++;}else if(*fmt==L't'){len=WLEN_T;fmt++;}else if(*fmt==L'L'){len=WLEN_CAPL;fmt++;}
        if(!*fmt)break;wchar_t conv=*fmt++;
        if(conv==L'n'){if(!suppress){void*p=va_arg(ap,void*);switch(len){case WLEN_HH:*(signed char*)p=(signed char)in->count;break;case WLEN_H:*(short*)p=(short)in->count;break;case WLEN_L:*(long*)p=(long)in->count;break;case WLEN_LL:*(long long*)p=(long long)in->count;break;case WLEN_J:*(intmax_t*)p=(intmax_t)in->count;break;case WLEN_Z:*(ptrdiff_t*)p=(ptrdiff_t)in->count;break;case WLEN_T:*(ptrdiff_t*)p=(ptrdiff_t)in->count;break;case WLEN_W:case WLEN_WF:switch(lenbits){case 8:if(len==WLEN_WF)*(int_fast8_t*)p=(int_fast8_t)in->count;else *(int8_t*)p=(int8_t)in->count;break;case 16:if(len==WLEN_WF)*(int_fast16_t*)p=(int_fast16_t)in->count;else *(int16_t*)p=(int16_t)in->count;break;case 32:if(len==WLEN_WF)*(int_fast32_t*)p=(int_fast32_t)in->count;else *(int32_t*)p=(int32_t)in->count;break;case 64:if(len==WLEN_WF)*(int_fast64_t*)p=(int_fast64_t)in->count;else *(int64_t*)p=(int64_t)in->count;break;default:*(int*)p=(int)in->count;}break;default:*(int*)p=(int)in->count;}}matched++;continue;}
        if(conv!=L'c'&&conv!=L'[')wscan_space(in);
        if(conv==L'c'||conv==L's'||conv==L'['){
            int invert=0;const wchar_t *set_start=NULL,*set_end=NULL;
            if(conv==L'['){if(*fmt==L'^'){invert=1;fmt++;}set_start=fmt;if(*fmt==L']')fmt++;while(*fmt&&*fmt!=L']')fmt++;if(*fmt!=L']')break;set_end=fmt;fmt++;}
            wchar_t *wout=(!suppress&&len==WLEN_L)?va_arg(ap,wchar_t*):NULL;
            char *out=(!suppress&&len!=WLEN_L)?va_arg(ap,char*):NULL;
            size_t n=0,lim=(conv==L'c'&&width==(size_t)-1)?1:width, bytes=0;mbstate_t state={{0,0}};int conversion_error=0;
            while(n<lim){wint_t c=wscan_take(in);if(c==WEOF){if(in->encoding_error)conversion_error=1;break;}if(conv==L's'&&iswspace(c)){wscan_back(in,c);break;}if(conv==L'['&&!wscan_set_member((wchar_t)c,set_start,set_end,invert)){wscan_back(in,c);break;}
                if(len==WLEN_L){if(!suppress)wout[n]=(wchar_t)c;}
                else {char b[4];size_t k=wcrtomb(b,(wchar_t)c,&state);if(k==(size_t)-1){wscan_back(in,c);in->encoding_error=1;conversion_error=1;break;}if(!suppress)for(size_t q=0;q<k;q++)out[bytes++]=b[q];}
                n++;
            }
            if(conversion_error)break;
            if(!n||(conv==L'c'&&width!=(size_t)-1&&n<width)){if(conv==L'c'&&n&&!in->encoding_error)in->eof=0;break;}
            if(!suppress){if(conv!=L'c'){if(wout)wout[n]=0;else out[bytes]=0;}assigned++;}matched++;continue;
        }
        if(wcschr(L"diuoxXbBp",conv)){
            wscan_buf b={0};size_t lim=width;wint_t c;int base=conv==L'o'?8:(conv==L'x'||conv==L'X'||conv==L'p'?16:(conv==L'b'||conv==L'B')?2:conv==L'i'?0:10),actual=base;
            c=b.n<lim?wscan_take(in):WEOF;if(c==L'+'||c==L'-')wscan_append(&b,(wchar_t)c);else wscan_back(in,c);
            wint_t first=b.n<lim?wscan_take(in):WEOF;int first_base=base?base:10;
            if(!wscan_digit(first,first_base)){wscan_back(in,first);if(b.n)in->eof=0;wscan_buf_free(&b);break;}
            wscan_append(&b,(wchar_t)first);if(actual==0)actual=first==L'0'?8:10;
            if(first==L'0'&&(base==0||base==16||base==2)&&b.n<lim){wint_t x=wscan_take(in);int pref=(x==L'x'||x==L'X')&&(base==0||base==16);pref|=(x==L'b'||x==L'B')&&(base==0||base==2);if(pref){int pbase=(x==L'b'||x==L'B')?2:16;wscan_append(&b,(pbase==2)?L'b':L'x');actual=pbase;if(b.n<lim){wint_t d=wscan_take(in);if(wscan_digit(d,pbase))wscan_append(&b,(wchar_t)d);else wscan_back(in,d);}}else wscan_back(in,x);}
            while(b.n<lim){c=wscan_take(in);if(wscan_digit(c,actual))wscan_append(&b,(wchar_t)c);else{wscan_back(in,c);break;}}
            if(b.error){wscan_buf_free(&b);return assigned;}
            wscan_append(&b,0);if(b.error){wscan_buf_free(&b);return assigned;}wchar_t *end;int signed_conv=conv==L'd'||conv==L'i';long long sv=signed_conv?wcstoll(b.p,&end,base):0;unsigned long long uv=signed_conv?0:wcstoull(b.p,&end,base);if(end!=b.p+b.n-1){if(b.n)in->eof=0;wscan_buf_free(&b);break;}
            if(!suppress){void*p=va_arg(ap,void*);if(conv==L'p')*(void**)p=(void*)(uintptr_t)uv;else if(signed_conv){switch(len){case WLEN_HH:*(signed char*)p=(signed char)sv;break;case WLEN_H:*(short*)p=(short)sv;break;case WLEN_L:*(long*)p=(long)sv;break;case WLEN_LL:*(long long*)p=sv;break;case WLEN_J:*(intmax_t*)p=(intmax_t)sv;break;case WLEN_Z:*(ptrdiff_t*)p=(ptrdiff_t)sv;break;case WLEN_T:*(ptrdiff_t*)p=(ptrdiff_t)sv;break;case WLEN_W:case WLEN_WF:switch(lenbits){case 8:if(len==WLEN_WF)*(int_fast8_t*)p=(int_fast8_t)sv;else *(int8_t*)p=(int8_t)sv;break;case 16:if(len==WLEN_WF)*(int_fast16_t*)p=(int_fast16_t)sv;else *(int16_t*)p=(int16_t)sv;break;case 32:if(len==WLEN_WF)*(int_fast32_t*)p=(int_fast32_t)sv;else *(int32_t*)p=(int32_t)sv;break;case 64:if(len==WLEN_WF)*(int_fast64_t*)p=(int_fast64_t)sv;else *(int64_t*)p=(int64_t)sv;break;default:*(int*)p=(int)sv;}break;default:*(int*)p=(int)sv;}}else{switch(len){case WLEN_HH:*(unsigned char*)p=(unsigned char)uv;break;case WLEN_H:*(unsigned short*)p=(unsigned short)uv;break;case WLEN_L:*(unsigned long*)p=(unsigned long)uv;break;case WLEN_LL:*(unsigned long long*)p=uv;break;case WLEN_J:*(uintmax_t*)p=(uintmax_t)uv;break;case WLEN_Z:*(size_t*)p=(size_t)uv;break;case WLEN_T:*(uintptr_t*)p=(uintptr_t)uv;break;case WLEN_W:case WLEN_WF:switch(lenbits){case 8:if(len==WLEN_WF)*(uint_fast8_t*)p=(uint_fast8_t)uv;else *(uint8_t*)p=(uint8_t)uv;break;case 16:if(len==WLEN_WF)*(uint_fast16_t*)p=(uint_fast16_t)uv;else *(uint16_t*)p=(uint16_t)uv;break;case 32:if(len==WLEN_WF)*(uint_fast32_t*)p=(uint_fast32_t)uv;else *(uint32_t*)p=(uint32_t)uv;break;case 64:if(len==WLEN_WF)*(uint_fast64_t*)p=(uint_fast64_t)uv;else *(uint64_t*)p=(uint64_t)uv;break;default:*(unsigned int*)p=(unsigned int)uv;}break;default:*(unsigned int*)p=(unsigned int)uv;}}assigned++;}wscan_buf_free(&b);matched++;continue;
        }
        if(wcschr(L"aAeEfFgG",conv)){
            wscan_buf b={0};wscan_float(in,width,&b);
            if(b.error){wscan_buf_free(&b);return assigned;}
            if(!b.n){wscan_buf_free(&b);break;}
            if(!wscan_append(&b,0)){wscan_buf_free(&b);break;}
            wchar_t *end;int valid;
            if(len==WLEN_CAPL){long double v=wcstold(b.p,&end);valid=end==b.p+b.n-1;if(valid&&!suppress)*(long double*)va_arg(ap,long double*)=v;}
            else if(len==WLEN_L){double v=wcstod(b.p,&end);valid=end==b.p+b.n-1;if(valid&&!suppress)*(double*)va_arg(ap,double*)=v;}
            else{float v=wcstof(b.p,&end);valid=end==b.p+b.n-1;if(valid&&!suppress)*(float*)va_arg(ap,float*)=v;}
            int consumed=b.n>0;wscan_buf_free(&b);if(!valid){if(consumed)in->eof=0;break;}if(!suppress)assigned++;matched++;continue;
        }
        break;
    }
    return wscan_finish(in,assigned,matched);
}

int vfwscanf(FILE *restrict f,const wchar_t *restrict fmt,__gnuc_va_list ap){if(!f||!fmt)return EOF;__libc_flockfile(f);__libc_fwide_unlocked(f,1);wscan_in in={.get=wscan_file_get,.unget=wscan_file_unget,.ctx=f};int r=wscan_core(&in,fmt,ap);__libc_funlockfile(f);return r;}
int fwscanf(FILE *restrict f,const wchar_t *restrict fmt,...){va_list ap;va_start(ap,fmt);int r=vfwscanf(f,fmt,ap);va_end(ap);return r;}
int vswscanf(const wchar_t *restrict s,const wchar_t *restrict fmt,__gnuc_va_list ap){if(!s||!fmt)return EOF;wscan_str str={s,0};wscan_in in={.get=wscan_str_get,.unget=wscan_str_unget,.ctx=&str};return wscan_core(&in,fmt,ap);}
int swscanf(const wchar_t *restrict s,const wchar_t *restrict fmt,...){va_list ap;va_start(ap,fmt);int r=vswscanf(s,fmt,ap);va_end(ap);return r;}
int vwscanf(const wchar_t *restrict fmt,__gnuc_va_list ap){return vfwscanf(stdin,fmt,ap);}
int wscanf(const wchar_t *restrict fmt,...){va_list ap;va_start(ap,fmt);int r=vwscanf(fmt,ap);va_end(ap);return r;}
