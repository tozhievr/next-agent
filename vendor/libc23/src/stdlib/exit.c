/* exit, _Exit, quick_exit, atexit, at_quick_exit, abort  (C23 7.24.4) */
#include "libc.h"

/* local declarations: this file must build independently of the public headers */
void *malloc(size_t);
void free(void *);
int raise(int);
void (*signal(int, void (*)(int)))(int);
#define SIGABRT 6
#define SIG_DFL ((void (*)(int))0)

#define STATIC_SLOTS 32

struct handler_node {
    struct handler_node *next;
    void (*fn[16])(void);
    int n;
};

struct handler_list {
    void (*first[STATIC_SLOTS])(void);
    int nfirst;
    struct handler_node *extra; /* most recent chunk first */
    libc_lock_t lock;
};

static struct handler_list atexit_list = { .lock = LIBC_LOCK_INIT };
static struct handler_list quick_list  = { .lock = LIBC_LOCK_INIT };

static int register_handler(struct handler_list *l, void (*fn)(void)) {
    int rc = 0;
    libc_lock(&l->lock);
    if (l->nfirst < STATIC_SLOTS) {
        l->first[l->nfirst++] = fn;
    } else if (l->extra && l->extra->n < 16) {
        l->extra->fn[l->extra->n++] = fn;
    } else {
        struct handler_node *nd = malloc(sizeof *nd);
        if (!nd) rc = -1;
        else {
            nd->next = l->extra; nd->n = 1; nd->fn[0] = fn;
            l->extra = nd;
        }
    }
    libc_unlock(&l->lock);
    return rc;
}

/* Runs handlers in reverse registration order.  Handlers registered while
 * running are also run (they are appended and popped first). */
static void run_handlers(struct handler_list *l) {
    for (;;) {
        void (*fn)(void) = NULL;
        libc_lock(&l->lock);
        if (l->extra) {
            struct handler_node *nd = l->extra;
            fn = nd->fn[--nd->n];
            if (nd->n == 0) { l->extra = nd->next; free(nd); }
        } else if (l->nfirst > 0) {
            fn = l->first[--l->nfirst];
        }
        libc_unlock(&l->lock);
        if (!fn) break;
        fn();
    }
}

int atexit(void (*fn)(void)) { return register_handler(&atexit_list, fn); }
int at_quick_exit(void (*fn)(void)) { return register_handler(&quick_list, fn); }

void __libc_run_atexit(void) { run_handlers(&atexit_list); }

_Noreturn void _Exit(int status) { pal_exit(status); }

_Noreturn void exit(int status) {
    __libc_run_atexit();
    __libc_stdio_close_all();
    pal_exit(status);
}

_Noreturn void quick_exit(int status) {
    run_handlers(&quick_list);
    _Exit(status);
}

_Noreturn void abort(void) {
    raise(SIGABRT);
    /* handler returned or SIGABRT ignored: terminate abnormally anyway */
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
    pal_abort();
}
