/* Platform layer: Windows (kernel32) or Linux (raw syscalls on libc23). */
#if defined(__linux__)
#include "platform_posix.c"
#else
#include "platform_win.c"
#endif
