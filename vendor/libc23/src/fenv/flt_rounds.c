/* Runtime value of FLT_ROUNDS, mapped from the active floating environment. */
#include <fenv.h>
int __libc_flt_rounds(void) {
    switch (fegetround()) {
    case FE_TONEAREST: return 1;
    case FE_TOWARDZERO: return 0;
    case FE_UPWARD: return 2;
    case FE_DOWNWARD: return 3;
    default: return -1;
    }
}
