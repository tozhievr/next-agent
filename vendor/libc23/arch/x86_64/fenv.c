/* <fenv.h> for x86_64.  SSE MXCSR is the primary state; the x87 control /
 * status word is also maintained where long double is the x87 type (every
 * platform except Windows, where long double == double). */
#if defined(__x86_64__)
#include <fenv.h>
#include <float.h>

#if defined(_WIN32)
#define HAVE_X87 0
#else
#define HAVE_X87 1
#endif

#define MX_FLAGS   0x3fu      /* IE DE ZE OE UE PE */
#define MX_MASKS   0x1f80u    /* exception mask bits (7..12) */
#define MX_DEFAULT 0x1f80u
#define X87_DEFAULT_CW 0x37f

static inline unsigned mx_get(void) { unsigned v; __asm__ volatile("stmxcsr %0" : "=m"(v)); return v; }
static inline void mx_set(unsigned v) { v &= 0xffffu; __asm__ volatile("ldmxcsr %0" : : "m"(v)); }

typedef struct { unsigned w[7]; } x87env;
static inline void x87_save(x87env *e) {
    __asm__ volatile("fnstenv %0\n\tfldenv %0" : "=m"(*e));
}
static inline void x87_load(const x87env *e) { __asm__ volatile("fldenv %0" : : "m"(*e)); }
static inline unsigned x87_sw(void) { unsigned short s; __asm__ volatile("fnstsw %0" : "=a"(s)); return s; }
static inline unsigned x87_cw(void) { unsigned short c; __asm__ volatile("fnstcw %0" : "=m"(c)); return c; }
static inline void x87_setcw(unsigned c) { unsigned short s = (unsigned short)c; __asm__ volatile("fldcw %0" : : "m"(s)); }

static int cur_flags(void) {
    unsigned f = mx_get() & MX_FLAGS;
    if (HAVE_X87) f |= x87_sw() & MX_FLAGS;
    return (int)(f & FE_ALL_EXCEPT);
}

int fetestexcept(int excepts) { return cur_flags() & excepts & FE_ALL_EXCEPT; }

int feclearexcept(int excepts) {
    unsigned e = (unsigned)excepts & FE_ALL_EXCEPT;
    mx_set(mx_get() & ~e);
    if (HAVE_X87) {
        x87env env;
        x87_save(&env);
        env.w[1] &= ~e;
        x87_load(&env);
    }
    return 0;
}

int fesetexcept(int excepts) {
    mx_set(mx_get() | ((unsigned)excepts & FE_ALL_EXCEPT));
    return 0;
}

int feraiseexcept(int excepts) {
    unsigned e = (unsigned)excepts & FE_ALL_EXCEPT;
    unsigned unmasked = ~(mx_get() >> 7) & MX_FLAGS;
    if (!(e & unmasked)) return fesetexcept((int)e);
    /* traps enabled for some of them: raise through real arithmetic */
    volatile double zero = 0.0, one = 1.0, big = DBL_MAX, tiny = DBL_MIN, r;
    if (e & FE_INVALID)   r = zero / zero;
    if (e & FE_DIVBYZERO) r = one / zero;
    if (e & FE_OVERFLOW)  r = big * big;
    if (e & FE_UNDERFLOW) r = tiny * tiny;
    if (e & FE_INEXACT)   r = one / 3.0;
    (void)r;
    return 0;
}

int fegetexceptflag(fexcept_t *flagp, int excepts) {
    *flagp = (fexcept_t)fetestexcept(excepts);
    return 0;
}
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
    int e = excepts & FE_ALL_EXCEPT;
    feclearexcept(e);
    return fesetexcept((int)(*flagp & (unsigned)e));
}
int fetestexceptflag(const fexcept_t *flagp, int excepts) {
    return (int)(*flagp & (unsigned)excepts & FE_ALL_EXCEPT);
}

int fegetround(void) { return (int)((mx_get() >> 3) & 0xc00u); }
int fesetround(int r) {
    if (r & ~0xc00) return 1;
    mx_set((mx_get() & ~0x6000u) | ((unsigned)r << 3));
    if (HAVE_X87) x87_setcw((x87_cw() & ~0xc00u) | (unsigned)r);
    return 0;
}

int fegetenv(fenv_t *envp) {
    for (int i = 0; i < 7; i++) envp->__x87[i] = 0;
    if (HAVE_X87) x87_save((x87env *)envp->__x87);
    envp->__mxcsr = mx_get();
    return 0;
}

int feholdexcept(fenv_t *envp) {
    fegetenv(envp);
    feclearexcept(FE_ALL_EXCEPT);
    mx_set(mx_get() | MX_MASKS);
    if (HAVE_X87) x87_setcw(x87_cw() | 0x3fu);
    return 0;
}

int fesetenv(const fenv_t *envp) {
    if (envp == FE_DFL_ENV) {
        mx_set(MX_DEFAULT);
        if (HAVE_X87) __asm__ volatile("fninit");
        return 0;
    }
    if (HAVE_X87) x87_load((const x87env *)envp->__x87);
    mx_set(envp->__mxcsr);
    return 0;
}

int feupdateenv(const fenv_t *envp) {
    int saved = cur_flags();
    fesetenv(envp);
    return feraiseexcept(saved);
}

int fegetmode(femode_t *modep) {
    modep->__mxcsr = mx_get() & ~MX_FLAGS;
    modep->__x87cw = (unsigned short)(HAVE_X87 ? x87_cw() : X87_DEFAULT_CW);
    modep->__pad = 0;
    return 0;
}

int fesetmode(const femode_t *modep) {
    unsigned mx, cw;
    if (modep == FE_DFL_MODE) { mx = MX_DEFAULT; cw = X87_DEFAULT_CW; }
    else { mx = modep->__mxcsr & ~MX_FLAGS; cw = modep->__x87cw; }
    mx_set((mx_get() & MX_FLAGS) | mx);
    if (HAVE_X87) x87_setcw(cw);
    return 0;
}

#endif
