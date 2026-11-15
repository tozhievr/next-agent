/* Platform layer: Windows (kernel32) or Linux (POSIX system calls). */
#if defined(__linux__)
#include "platform_posix.cpp"
#else
#include "platform_win.cpp"
#endif
