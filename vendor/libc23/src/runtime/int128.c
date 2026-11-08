/* Software division for Clang's 128-bit compiler ABI, without a host CRT. */
#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 u128;
typedef __int128 s128;

static u128 divide(u128 n, u128 d, u128 *remainder) {
    if (!d) __builtin_trap();
    u128 q=0, r=0;
    for (int i=127;i>=0;--i) {
        unsigned high=(unsigned)(r>>127);
        r=(r<<1) | ((n>>i)&1);
        if (high || r>=d) { r-=d; q|=(u128)1<<i; }
    }
    if (remainder) *remainder=r;
    return q;
}
static s128 signed_bits(u128 x) { union { u128 u; s128 s; } v={.u=x}; return v.s; }
u128 __udivmodti4(u128 a,u128 b,u128 *r) { return divide(a,b,r); }
u128 __udivti3(u128 a,u128 b) { return divide(a,b,0); }
u128 __umodti3(u128 a,u128 b) { u128 r; divide(a,b,&r); return r; }
s128 __divmodti4(s128 a,s128 b,s128 *r) {
    u128 ua=(u128)a, ub=(u128)b;
    int na=a<0, nb=b<0;
    if (na) ua=0-ua;
    if (nb) ub=0-ub;
    u128 rem, q=divide(ua,ub,&rem);
    if (na!=nb) q=0-q;
    if (na) rem=0-rem;
    if (r) *r=signed_bits(rem);
    return signed_bits(q);
}
s128 __divti3(s128 a,s128 b) { return __divmodti4(a,b,0); }
s128 __modti3(s128 a,s128 b) { s128 r; __divmodti4(a,b,&r); return r; }
#endif
