/* <signal.h> - C23 7.14 (libc23) */
#ifndef _LIBC23_SIGNAL_H
#define _LIBC23_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGINT   2
#define SIGILL   4
#define SIGABRT  6
#define SIGFPE   8
#define SIGSEGV 11
#define SIGTERM 15

void (*signal(int __sig, void (*__func)(int)))(int);
int raise(int __sig);

#ifdef __cplusplus
}
#endif
#endif
