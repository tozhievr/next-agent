/* <fenv.h> for aarch64: FPCR (modes) and FPSR (sticky flags). */
#if defined(__aarch64__)
#include <fenv.h>
#include <float.h>

#define FLAGS 0x1fu
#define TRAPS_SHIFT 8

static inline unsigned long long rd_fpcr(void) { unsigned long long v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline unsigned long long rd_fpsr(void) { unsigned long long v; __asm__ volatile("mrs %0, fpsr" : "=r"(v)); return v; }
static inline void wr_fpcr(unsigned long long v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
static inline void wr_fpsr(unsigned long long v) { __asm__ volatile("msr fpsr, %0" : : "r"(v)); }

int fetestexcept(int excepts) { return (int)(rd_fpsr() & FLAGS & (unsigned)excepts); }
int feclearexcept(int excepts) { wr_fpsr(rd_fpsr() & ~(unsigned long long)((unsigned)excepts & FLAGS)); return 0; }
int fesetexcept(int excepts) { wr_fpsr(rd_fpsr() | ((unsigned)excepts & FLAGS)); return 0; }

int feraiseexcept(int excepts) {
    unsigned e = (unsigned)excepts & FLAGS;
    unsigned trapped = (unsigned)(rd_fpcr() >> TRAPS_SHIFT) & FLAGS;
    if (!(e & trapped)) return fesetexcept((int)e);
    volatile double zero = 0.0, one = 1.0, big = DBL_MAX, tiny = DBL_MIN, r;
    if (e & FE_INVALID)   r = zero / zero;
    if (e & FE_DIVBYZERO) r = one / zero;
    if (e & FE_OVERFLOW)  r = big * big;
    if (e & FE_UNDERFLOW) r = tiny * tiny;
    if (e & FE_INEXACT)   r = one / 3.0;
    (void)r;
    return 0;
}

int fegetexceptflag(fexcept_t *flagp, int excepts) { *flagp = (fexcept_t)fetestexcept(excepts); return 0; }
int fesetexceptflag(const fexcept_t *flagp, int excepts) {
    int e = excepts & FE_ALL_EXCEPT;
    feclearexcept(e);
    return fesetexcept((int)(*flagp & (unsigned)e));
}
int fetestexceptflag(const fexcept_t *flagp, int excepts) { return (int)(*flagp & (unsigned)excepts & FLAGS); }

int fegetround(void) { return (int)(rd_fpcr() & 0xc00000u); }
int fesetround(int r) {
    if (r & ~0xc00000) return 1;
    wr_fpcr((rd_fpcr() & ~0xc00000ull) | (unsigned)r);
    return 0;
}

int fegetenv(fenv_t *envp) { envp->__fpcr = (unsigned)rd_fpcr(); envp->__fpsr = (unsigned)rd_fpsr(); return 0; }
int feholdexcept(fenv_t *envp) {
    fegetenv(envp);
    wr_fpsr(rd_fpsr() & ~(unsigned long long)FLAGS);
    wr_fpcr(rd_fpcr() & ~((unsigned long long)FLAGS << TRAPS_SHIFT));
    return 0;
}
int fesetenv(const fenv_t *envp) {
    if (envp == FE_DFL_ENV) { wr_fpcr(0); wr_fpsr(0); return 0; }
    wr_fpcr(envp->__fpcr); wr_fpsr(envp->__fpsr);
    return 0;
}
int feupdateenv(const fenv_t *envp) {
    int saved = (int)(rd_fpsr() & FLAGS);
    fesetenv(envp);
    return feraiseexcept(saved);
}

int fegetmode(femode_t *modep) { modep->__fpcr = (unsigned)rd_fpcr(); return 0; }
int fesetmode(const femode_t *modep) { wr_fpcr(modep == FE_DFL_MODE ? 0 : modep->__fpcr); return 0; }

#endif
