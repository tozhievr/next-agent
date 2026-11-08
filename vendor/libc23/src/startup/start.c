/* Portable runtime entry, called by the platform backend (see platform/pal.h). */
#include "libc.h"

extern int main(int, char **, char **);
_Noreturn void exit(int);

_Noreturn void __libc_start_main(int argc, char **argv, char **envp) {
    (void)envp;
    __libc_locale_init();
    __libc_signal_init();
    exit(main(argc, argv, pal_environ));
}
