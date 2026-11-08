/* Compiler atomic runtime for objects that cannot use lock-free instructions. */
#include <stddef.h>
#include <stdint.h>
#include "platform/pal.h"

#define LOCK_COUNT 127
static int locks[LOCK_COUNT];
static int *lock_for(const volatile void *p) { return &locks[((uintptr_t)p >> 3) % LOCK_COUNT]; }
static void acquire(int *p) { while (__atomic_exchange_n(p,1,__ATOMIC_SEQ_CST)) pal_futex_wait(p,1,-1); }
static void release(int *p) { __atomic_store_n(p,0,__ATOMIC_SEQ_CST); pal_futex_wake(p,1); }
static void copy_bytes(volatile void *dst, const volatile void *src, size_t n) {
    volatile unsigned char *d=dst; const volatile unsigned char *s=src;
    for (size_t i=0;i<n;i++) d[i]=s[i];
}
static int same_bytes(const volatile void *a,const volatile void *b,size_t n) {
    const volatile unsigned char *x=a,*y=b;
    for (size_t i=0;i<n;i++) if(x[i]!=y[i]) return 0;
    return 1;
}
void __libc_atomic_load(size_t n,const volatile void *p,void *out,int order) __asm__("__atomic_load");
void __libc_atomic_load(size_t n,const volatile void *p,void *out,int order) {
    (void)order; int *l=lock_for(p); acquire(l); copy_bytes(out,p,n); release(l);
}
void __libc_atomic_store(size_t n,volatile void *p,const void *value,int order) __asm__("__atomic_store");
void __libc_atomic_store(size_t n,volatile void *p,const void *value,int order) {
    (void)order; int *l=lock_for(p); acquire(l); copy_bytes(p,value,n); release(l);
}
void __libc_atomic_exchange(size_t n,volatile void *p,const void *value,void *out,int order) __asm__("__atomic_exchange");
void __libc_atomic_exchange(size_t n,volatile void *p,const void *value,void *out,int order) {
    (void)order; int *l=lock_for(p); acquire(l); copy_bytes(out,p,n); copy_bytes(p,value,n); release(l);
}
_Bool __libc_atomic_compare(size_t n,volatile void *p,void *expected,const void *desired,int success,int failure) __asm__("__atomic_compare_exchange");
_Bool __libc_atomic_compare(size_t n,volatile void *p,void *expected,const void *desired,int success,int failure) {
    (void)success; (void)failure; int *l=lock_for(p); acquire(l); int ok=same_bytes(p,expected,n);
    if (ok) copy_bytes(p,desired,n); else copy_bytes(expected,p,n); release(l); return ok;
}
_Bool __libc_atomic_is_lock_free(size_t n,const volatile void *p) __asm__("__atomic_is_lock_free");
_Bool __libc_atomic_is_lock_free(size_t n,const volatile void *p) {
    return (n==1 || n==2 || n==4 || n==8) && ((uintptr_t)p % n == 0);
}
#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 u128;
u128 __libc_load16(const volatile void *p,int order) __asm__("__atomic_load_16");
u128 __libc_load16(const volatile void *p,int order) { u128 out; __libc_atomic_load(16,p,&out,order); return out; }
void __libc_store16(volatile void *p,u128 v,int order) __asm__("__atomic_store_16");
void __libc_store16(volatile void *p,u128 v,int order) { __libc_atomic_store(16,p,&v,order); }
u128 __libc_exchange16(volatile void *p,u128 v,int order) __asm__("__atomic_exchange_16");
u128 __libc_exchange16(volatile void *p,u128 v,int order) { u128 out; __libc_atomic_exchange(16,p,&v,&out,order); return out; }
_Bool __libc_compare16(volatile void *p,void *expected,u128 v,int success,int failure) __asm__("__atomic_compare_exchange_16");
_Bool __libc_compare16(volatile void *p,void *expected,u128 v,int success,int failure) { return __libc_atomic_compare(16,p,expected,&v,success,failure); }
#define RMW(name, expr) \
u128 __libc_##name##16(volatile void *p,u128 value,int order) __asm__("__atomic_fetch_" #name "_16"); \
u128 __libc_##name##16(volatile void *p,u128 value,int order) { \
    (void)order; int *l=lock_for(p); acquire(l); u128 old; copy_bytes(&old,p,16); \
    u128 next=(expr); copy_bytes(p,&next,16); release(l); return old; }
RMW(add, old+value)
RMW(sub, old-value)
RMW(and, old&value)
RMW(or, old|value)
RMW(xor, old^value)
RMW(nand, ~(old&value))
#undef RMW
#endif
