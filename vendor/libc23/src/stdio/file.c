#include "file.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

extern int __libc_locale_utf8;
char *__libc_strerror_message(int, char *) LIBC_HIDDEN;

#define F_USED 0x0200u /* an operation has been performed since association */

static FILE in_file = {.flags=F_RD|F_STATIC|F_LAZYFD,.stdno=0,.bufmode=BM_AUTO};
static FILE out_file = {.flags=F_WR|F_STATIC|F_LAZYFD,.stdno=1,.bufmode=BM_AUTO};
static FILE err_file = {.flags=F_WR|F_STATIC|F_LAZYFD,.stdno=2,.bufmode=_IONBF};
FILE *stdin=&in_file, *stdout=&out_file, *stderr=&err_file;
static FILE *files;
static libc_lock_t files_lock=LIBC_LOCK_INIT;
void __libc_flockfile(FILE *f){libc_rlock(&f->lock);}
void __libc_funlockfile(FILE *f){libc_runlock(&f->lock);}
pal_file_t __stdio_resolve_fd(FILE *f){f->fd=pal_std_file(f->stdno); f->flags &= ~F_LAZYFD; return f->fd;}
void __stdio_ofl_add(FILE *f){libc_lock(&files_lock); f->next=files; if(files) files->prev=f; files=f; libc_unlock(&files_lock);}
void __stdio_ofl_remove(FILE *f){libc_lock(&files_lock); if(f->prev) f->prev->next=f->next; else if(files==f) files=f->next; if(f->next) f->next->prev=f->prev; f->next=f->prev=0; libc_unlock(&files_lock);}
FILE *__stdio_new_file(void){FILE *f=calloc(1,sizeof *f); if(f) f->bufmode=BM_AUTO; return f;}
int __stdio_parse_mode(const char *m,int *p,unsigned *s){
 if(!m || !*m) return libc_seterr(EINVAL); int flags; unsigned sf;
 switch(*m++){case 'r':flags=PAL_O_RDONLY;sf=F_RD;break;case 'w':flags=PAL_O_WRONLY|PAL_O_CREAT|PAL_O_TRUNC;sf=F_WR;break;case 'a':flags=PAL_O_WRONLY|PAL_O_CREAT|PAL_O_APPEND;sf=F_WR|F_APP;break;default:return libc_seterr(EINVAL);}
 int plus=0,b=0,x=0; for(;*m;m++){if(*m=='+'&&!plus)plus=1;else if(*m=='b'&&!b)b=1;else if(*m=='x'&&!x)x=1;else return libc_seterr(EINVAL);} if(x && !(flags&PAL_O_CREAT))return libc_seterr(EINVAL); if(x)flags|=PAL_O_EXCL; if(plus){flags=(flags&~PAL_O_RDWR)|PAL_O_RDWR;sf|=F_RD|F_WR;} *p=flags;*s=sf;return 0;
}
static void setup(FILE *f){f->flags|=F_USED;if(f->buf)return; if(f->bufmode==BM_AUTO)f->bufmode=pal_isatty(FD(f))?_IOLBF:_IOFBF; if(f->bufmode!=_IONBF){f->buf=malloc(BUFSIZ);if(f->buf){f->bufsize=BUFSIZ;f->flags|=F_OWNBUF;}} if(!f->buf){f->buf=f->onebuf;f->bufsize=1;f->bufmode=_IONBF;}f->rpos=f->rend=f->wpos=f->buf;}
int __stdio_flushw(FILE *f){if(f->mode!=M_WRITE||!f->buf)return 0; size_t n=(size_t)(f->wpos-f->buf), done=0;while(done<n){size_t k=0;int rc=pal_write(FD(f),f->buf+done,n-done,&k);if(rc<0||!k){if(rc<0)errno=-rc;else errno=EIO;f->flags|=F_ERR;if(done){memmove(f->buf,f->buf+done,n-done);f->wpos=f->buf+n-done;}return EOF;}done+=k;}f->wpos=f->buf;return 0;}
static int drop_read_checked(FILE *f){if(f->mode==M_READ){int64_t n=(int64_t)(f->rend-f->rpos)+f->ucnt;if(n){int64_t pos;int rc=pal_seek(FD(f),-n,PAL_SEEK_CUR,&pos);if(rc<0){errno=-rc;return EOF;}}f->rpos=f->rend=f->buf;f->ucnt=0;f->wucnt=0;}return 0;}
void __stdio_drop_read(FILE *f){(void)drop_read_checked(f);}
int __stdio_to_read(FILE *f){if(!(f->flags&F_RD)){f->flags|=F_ERR;return libc_seterr(EBADF);}setup(f);if(f->mode==M_WRITE&&__stdio_flushw(f))return EOF; if(f->mode!=M_READ){f->rpos=f->rend=f->buf;f->mode=M_READ;}return 0;}
int __stdio_to_write(FILE *f){if(!(f->flags&F_WR)){f->flags|=F_ERR;return libc_seterr(EBADF);}setup(f);if(f->mode==M_READ&&drop_read_checked(f)){f->flags|=F_ERR;return EOF;} if(f->mode!=M_WRITE){if(f->flags&F_APP){int saved=errno;int64_t pos;(void)pal_seek(FD(f),0,PAL_SEEK_END,&pos);errno=saved;}f->wpos=f->buf;f->mode=M_WRITE;}return 0;}
int __stdio_refill(FILE *f){if(__stdio_to_read(f))return EOF;size_t n=0;int rc=pal_read(FD(f),f->buf,f->bufsize,&n);if(rc<0){f->flags|=F_ERR;errno=-rc;return EOF;}if(!n){f->flags|=F_EOF;return EOF;}f->rpos=f->buf;f->rend=f->buf+n;return 0;}
size_t __stdio_write_bytes(FILE *f,const unsigned char *p,size_t n){if(__stdio_to_write(f))return 0;size_t i=0;while(i<n){if(f->wpos==f->buf+f->bufsize && __stdio_flushw(f))break;unsigned char c=p[i++];*f->wpos++=c;if((f->bufmode==_IONBF || (f->bufmode==_IOLBF&&c=='\n'))&&__stdio_flushw(f)){--i;break;}}return i;}
int __libc_fwide_unlocked(FILE *f,int o){f->flags|=F_USED;if(!f->orient&&o){f->orient=o>0?1:-1;if(f->orient>0)f->wide_utf8=!!__libc_locale_utf8;}return f->orient;}
int __libc_getc_unlocked(FILE *f){if(__libc_fwide_unlocked(f,-1)>0){f->flags|=F_ERR;return EOF;}return __stdio_getb(f);}
int __libc_ungetc_unlocked(int c,FILE *f){if(c==EOF||__libc_fwide_unlocked(f,-1)>0||f->ucnt==UNGET_MAX)return EOF;if(__stdio_to_read(f))return EOF;f->unget[f->ucnt++]=(unsigned char)c;f->flags&=~F_EOF;return (unsigned char)c;}
int __libc_putc_unlocked(int c,FILE *f){unsigned char b=(unsigned char)c;if(__libc_fwide_unlocked(f,-1)>0){f->flags|=F_ERR;return EOF;}return __stdio_write_bytes(f,&b,1)==1?b:EOF;}
size_t __libc_fwrite_unlocked(const void *p,size_t n,FILE *f){if(__libc_fwide_unlocked(f,-1)>0){f->flags|=F_ERR;return 0;}return __stdio_write_bytes(f,p,n);}
size_t __libc_fread_unlocked(void *p,size_t n,FILE *f){unsigned char *s=p;size_t i=0;for(;i<n;i++){int c=__libc_getc_unlocked(f);if(c==EOF)break;s[i]=(unsigned char)c;}return i;}
int __libc_ferror_unlocked(FILE *f){return !!(f->flags&F_ERR);}
int __libc_feof_unlocked(FILE *f){return !!(f->flags&F_EOF);}
FILE *fopen(const char *name,const char *mode){int pf;unsigned ff;pal_file_t fd;if(__stdio_parse_mode(mode,&pf,&ff))return 0;int r=pal_open(name,pf,&fd);if(r<0){errno=-r;return 0;}FILE *f=__stdio_new_file();if(!f){pal_close(fd);return 0;}f->fd=fd;f->flags=ff;size_t n=strlen(name)+1;f->path=malloc(n);if(f->path)memcpy(f->path,name,n);__stdio_ofl_add(f);return f;}
int fclose(FILE *f){
 __stdio_ofl_remove(f);
 __libc_flockfile(f);
 int isstatic=!!(f->flags&F_STATIC),r=__stdio_flushw(f);
 int rc=pal_close(FD(f));if(rc<0){errno=-rc;r=EOF;}
 if(f->flags&F_OWNBUF)free(f->buf);
 free(f->path);f->path=0;f->buf=0;f->mode=M_NONE;
 f->flags=isstatic?F_STATIC:0;f->fd=PAL_INVALID_FILE;
 __libc_funlockfile(f);
 if(!isstatic)free(f);
 return r;
}
FILE *freopen(const char *name,const char *mode,FILE *f){
 int pf;unsigned ff;if(__stdio_parse_mode(mode,&pf,&ff))return 0;
 __libc_flockfile(f);
 int isstatic=!!(f->flags&F_STATIC),copy_failed=0;char *copy=0;
 if(!name&&f->path){size_t n=strlen(f->path)+1;copy=malloc(n);if(copy)memcpy(copy,f->path,n);else copy_failed=1;name=copy;}
 (void)__stdio_flushw(f);(void)pal_close(FD(f));
 pal_file_t fd;int rc=name?pal_open(name,pf,&fd):(copy_failed?-ENOMEM:-EINVAL);
 if(rc<0){
  if(f->flags&F_OWNBUF)free(f->buf);free(f->path);free(copy);
  f->path=0;f->buf=0;f->mode=M_NONE;f->fd=PAL_INVALID_FILE;f->flags=isstatic?F_STATIC:0;
  __libc_funlockfile(f);
  if(!isstatic){__stdio_ofl_remove(f);free(f);}
  errno=-rc;return 0;
 }
 free(f->path);f->path=0;
 if(name){size_t n=strlen(name)+1;f->path=malloc(n);if(f->path)memcpy(f->path,name,n);}
 free(copy);f->flags=(f->flags&(F_STATIC|F_OWNBUF))|ff;f->fd=fd;
 f->mode=M_NONE;f->orient=0;f->ucnt=f->wucnt=0;memset(&f->mbs,0,sizeof f->mbs);
 __libc_funlockfile(f);return f;
}
static int flush_static_if_open(FILE *f){
 __libc_flockfile(f);int r=0;
 if((f->flags&F_WR)&&f->mode!=M_READ){f->flags|=F_USED;r=__stdio_flushw(f);}
 __libc_funlockfile(f);return r;
}
int fflush(FILE *f){if(!f){int r=0;libc_lock(&files_lock);for(FILE *p=files;p;p=p->next){__libc_flockfile(p);if((p->flags&F_WR)&&p->mode!=M_READ){p->flags|=F_USED;if(__stdio_flushw(p))r=EOF;}__libc_funlockfile(p);}libc_unlock(&files_lock);if(flush_static_if_open(stdin))r=EOF;if(flush_static_if_open(stdout))r=EOF;if(flush_static_if_open(stderr))r=EOF;return r;}__libc_flockfile(f);f->flags|=F_USED;int r=__stdio_flushw(f);__libc_funlockfile(f);return r;}
void __libc_stdio_flush_all(void){fflush(0);}void __libc_stdio_close_all(void){fflush(0);while(files)fclose(files);}
int setvbuf(FILE *f,char *buf,int mode,size_t n){if(mode!=_IOFBF&&mode!=_IOLBF&&mode!=_IONBF)return -1;__libc_flockfile(f);if(f->mode!=M_NONE||(f->flags&F_USED)){__libc_funlockfile(f);return -1;}if(f->flags&F_OWNBUF)free(f->buf);f->flags&=~F_OWNBUF;f->buf=0;f->bufmode=mode;if(buf&&n&&mode!=_IONBF){f->buf=(unsigned char *)buf;f->bufsize=n;f->rpos=f->rend=f->wpos=f->buf;}__libc_funlockfile(f);return 0;}
void setbuf(FILE *f,char *b){(void)setvbuf(f,b,b?_IOFBF:_IONBF,BUFSIZ);}
int fgetc(FILE *f){__libc_flockfile(f);int c=__libc_getc_unlocked(f);__libc_funlockfile(f);return c;}int getc(FILE *f){return fgetc(f);}int getchar(void){return fgetc(stdin);}int ungetc(int c,FILE *f){__libc_flockfile(f);int r=__libc_ungetc_unlocked(c,f);__libc_funlockfile(f);return r;}
int fputc(int c,FILE *f){__libc_flockfile(f);int r=__libc_putc_unlocked(c,f);__libc_funlockfile(f);return r;}int putc(int c,FILE *f){return fputc(c,f);}int putchar(int c){return fputc(c,stdout);}
char *fgets(char *s,int n,FILE *f){if(n<=0)return 0;__libc_flockfile(f);unsigned old_error=f->flags&F_ERR;f->flags&=~F_ERR;int i=0,c=0;while(i<n-1&&(c=__libc_getc_unlocked(f))!=EOF){s[i++]=(char)c;if(c=='\n')break;}int failed=c==EOF&&__libc_ferror_unlocked(f);f->flags|=old_error;if(!failed&&(i||n==1))s[i]=0;__libc_funlockfile(f);return !failed&&(i||n==1)?s:0;}
int fputs(const char *s,FILE *f){size_t n=strlen(s);__libc_flockfile(f);size_t r=__libc_fwrite_unlocked(s,n,f);__libc_funlockfile(f);return r==n?0:EOF;}int puts(const char *s){__libc_flockfile(stdout);size_t n=strlen(s);int r=__libc_fwrite_unlocked(s,n,stdout)==n&&__libc_putc_unlocked('\n',stdout)!=EOF?0:EOF;__libc_funlockfile(stdout);return r;}
size_t fread(void *p,size_t s,size_t n,FILE *f){if(!s||!n)return 0;if(n>SIZE_MAX/s){errno=EOVERFLOW;return 0;}__libc_flockfile(f);size_t r=__libc_fread_unlocked(p,s*n,f);__libc_funlockfile(f);return r/s;}size_t fwrite(const void *p,size_t s,size_t n,FILE *f){if(!s||!n)return 0;if(n>SIZE_MAX/s){errno=EOVERFLOW;return 0;}__libc_flockfile(f);size_t r=__libc_fwrite_unlocked(p,s*n,f);__libc_funlockfile(f);return r/s;}
int64_t __stdio_tell(FILE *f){f->flags|=F_USED;int64_t pos;int rc=pal_seek(FD(f),0,PAL_SEEK_CUR,&pos);if(rc<0){errno=-rc;return -1;}if(f->mode==M_READ){int64_t unread=(int64_t)(f->rend-f->rpos)+f->ucnt;if(pos<INT64_MIN+unread){errno=EOVERFLOW;return -1;}pos-=unread;}else if(f->mode==M_WRITE&&f->buf){int64_t pending=(int64_t)(f->wpos-f->buf);if(pos>INT64_MAX-pending){errno=EOVERFLOW;return -1;}pos+=pending;}return pos;}
int __stdio_seek(FILE *f,int64_t off,int whence){f->flags|=F_USED;if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END)return libc_seterr(EINVAL);if(f->mode==M_WRITE&&__stdio_flushw(f))return EOF;if(f->mode==M_READ&&whence==SEEK_CUR){int64_t unread=(int64_t)(f->rend-f->rpos)+f->ucnt;if(off<INT64_MIN+unread)return libc_seterr(EOVERFLOW);off-=unread;}int64_t pos;int rc=pal_seek(FD(f),off,whence,&pos);if(rc<0){errno=-rc;return EOF;}f->mode=M_NONE;f->ucnt=f->wucnt=0;f->flags&=~F_EOF;memset(&f->mbs,0,sizeof f->mbs);return 0;}
int fseek(FILE *f,long off,int whence){__libc_flockfile(f);int r=__stdio_seek(f,off,whence);__libc_funlockfile(f);return r;}long ftell(FILE *f){__libc_flockfile(f);int64_t n=__stdio_tell(f);__libc_funlockfile(f);if(n>LONG_MAX){errno=EOVERFLOW;return -1;}if(n<LONG_MIN){errno=EOVERFLOW;return -1;}return (long)n;}void rewind(FILE *f){(void)fseek(f,0,SEEK_SET);clearerr(f);}int fgetpos(FILE *f,fpos_t *p){__libc_flockfile(f);p->__off=__stdio_tell(f);p->__mbs=f->mbs;__libc_funlockfile(f);return p->__off<0?-1:0;}int fsetpos(FILE *f,const fpos_t *p){__libc_flockfile(f);int r=__stdio_seek(f,p->__off,SEEK_SET);if(!r)f->mbs=p->__mbs;__libc_funlockfile(f);return r;}
void clearerr(FILE *f){__libc_flockfile(f);f->flags|=F_USED;f->flags&=~(F_ERR|F_EOF);__libc_funlockfile(f);}int feof(FILE *f){__libc_flockfile(f);f->flags|=F_USED;int r=__libc_feof_unlocked(f);__libc_funlockfile(f);return r;}int ferror(FILE *f){__libc_flockfile(f);f->flags|=F_USED;int r=__libc_ferror_unlocked(f);__libc_funlockfile(f);return r;}
int remove(const char *p){int r=pal_remove(p);return r<0?libc_pal_fail(r):0;}int rename(const char *a,const char *b){int r=pal_rename(a,b);return r<0?libc_pal_fail(r):0;}void perror(const char *s){
 int saved=errno;char buf[96];const char *message=__libc_strerror_message(saved,buf);
 __libc_flockfile(stderr);int ok=1;
 if(s&&*s){size_t n=strlen(s);if(__libc_fwrite_unlocked(s,n,stderr)!=n||__libc_fwrite_unlocked(": ",2,stderr)!=2)ok=0;}
 if(ok){size_t n=strlen(message);if(__libc_fwrite_unlocked(message,n,stderr)!=n)ok=0;}
 if(ok&&__libc_putc_unlocked('\n',stderr)==EOF)ok=0;
 __libc_funlockfile(stderr);if(ok)errno=saved;
}
/* One random process nonce and a strictly increasing serial avoid repeats,
 * even if the random source produces the same bytes on every call. */
static libc_lock_t temp_lock=LIBC_LOCK_INIT;
static libc_lock_t tmpnam_lock=LIBC_LOCK_INIT;
static unsigned char temp_nonce[8];
static int temp_nonce_ready;
static uint64_t temp_serial;
static int tempname(char *s){
 char dir[L_tmpnam];int rc=pal_tmpdir(dir,sizeof dir);if(rc<0)return libc_pal_fail(rc);
 size_t n=strlen(dir);if(n+7+16+16+1>L_tmpnam)return libc_seterr(ERANGE);
 libc_lock(&temp_lock);
 if(!temp_nonce_ready){rc=pal_random(temp_nonce,sizeof temp_nonce);if(rc<0){libc_unlock(&temp_lock);return libc_pal_fail(rc);}temp_nonce_ready=1;}
 if(temp_serial==UINT64_MAX){libc_unlock(&temp_lock);return libc_seterr(EOVERFLOW);}
 uint64_t serial=++temp_serial;memcpy(s,dir,n);memcpy(s+n,"libc23-",7);n+=7;
 static const char hex[]="0123456789abcdef";
 for(int i=0;i<8;i++){s[n++]=hex[temp_nonce[i]>>4];s[n++]=hex[temp_nonce[i]&15];}
 for(int i=15;i>=0;i--)s[n++]=hex[(serial>>(i*4))&15];
 s[n]=0;libc_unlock(&temp_lock);return 0;
}
char *tmpnam(char *s){
 static char name[L_tmpnam];libc_lock(&tmpnam_lock);if(!s)s=name;
 for(;;){
  if(tempname(s)){libc_unlock(&tmpnam_lock);return 0;}
  pal_file_t fd;int rc=pal_open(s,PAL_O_RDONLY,&fd);
  if(rc==-ENOENT){libc_unlock(&tmpnam_lock);return s;}
  if(rc<0){errno=-rc;libc_unlock(&tmpnam_lock);return 0;}
  rc=pal_close(fd);if(rc<0){errno=-rc;libc_unlock(&tmpnam_lock);return 0;}
 }
}FILE *tmpfile(void){char name[L_tmpnam];for(int i=0;i<100;i++){if(tempname(name))return 0;pal_file_t fd;int rc=pal_open(name,PAL_O_RDWR|PAL_O_CREAT|PAL_O_EXCL|PAL_O_TEMP,&fd);if(rc==-EEXIST)continue;if(rc<0){errno=-rc;return 0;}FILE *f=__stdio_new_file();if(!f){pal_close(fd);return 0;}f->fd=fd;f->flags=F_RD|F_WR;__stdio_ofl_add(f);return f;}errno=EEXIST;return 0;}
int __libc_wide_getbyte_unlocked(FILE *f){return __stdio_getb(f);}
int __libc_wide_putbyte_unlocked(int c,FILE *f){unsigned char b=(unsigned char)c;return __stdio_write_bytes(f,&b,1)==1?b:EOF;}
int __libc_wide_unget_bytes_unlocked(const unsigned char *s,size_t n,FILE *f){if(__stdio_to_read(f)||n>(size_t)(UNGET_MAX-f->ucnt))return 0;while(n)f->unget[f->ucnt++]=s[--n];f->flags&=~F_EOF;return 1;}
void __libc_wide_error_unlocked(FILE *f){f->flags|=F_ERR;}
size_t __stdio_wide_unget_bytes(const FILE *f){(void)f;return 0;}








