#include <threads.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include "platform/pal.h"

#define KEY_COUNT 1024
struct key_slot { unsigned generation; int used; tss_dtor_t dtor; };
struct value { struct value *next; unsigned key, generation; void *value; };
static struct key_slot keys[KEY_COUNT];
static int key_lock;
static _Thread_local struct value *values;
static void lock_keys(void) { while (__atomic_exchange_n(&key_lock, 1, __ATOMIC_ACQUIRE)) pal_futex_wait(&key_lock, 1, -1); }
static void unlock_keys(void) { __atomic_store_n(&key_lock, 0, __ATOMIC_RELEASE); pal_futex_wake(&key_lock, 1); }
int tss_create(tss_t *key, tss_dtor_t dtor) {
    lock_keys();
    for (unsigned i = 0; i < KEY_COUNT; ++i) if (!keys[i].used) {
        keys[i].used = 1; keys[i].dtor = dtor; ++keys[i].generation;
        *key = i; unlock_keys(); return thrd_success;
    }
    unlock_keys(); return thrd_error;
}
void tss_delete(tss_t key) { if (key >= KEY_COUNT) return; lock_keys(); keys[key].used = 0; keys[key].dtor = 0; unlock_keys(); }
void *tss_get(tss_t key) {
    if (key >= KEY_COUNT) return 0;
    lock_keys(); unsigned generation = keys[key].generation; int used = keys[key].used; unlock_keys();
    if (used) for (struct value *v = values; v; v = v->next) if (v->key == key && v->generation == generation) return v->value;
    return 0;
}
int tss_set(tss_t key, void *val) {
    if (key >= KEY_COUNT) return thrd_error;
    lock_keys(); unsigned generation = keys[key].generation; int used = keys[key].used; unlock_keys();
    if (!used) return thrd_error;
    for (struct value *v = values; v; v = v->next) if (v->key == key) { v->generation = generation; v->value = val; return thrd_success; }
    struct value *v = malloc(sizeof *v); if (!v) return thrd_nomem;
    v->key = key; v->generation = generation; v->value = val; v->next = values; values = v; return thrd_success;
}
void __libc_tss_run_dtors(void) {
    for (int pass = 0; pass < TSS_DTOR_ITERATIONS; ++pass) {
        int called = 0;
        for (struct value *v = values; v; v = v->next) {
            lock_keys(); tss_dtor_t dtor = keys[v->key].used && keys[v->key].generation == v->generation ? keys[v->key].dtor : 0; unlock_keys();
            if (dtor && v->value) { void *p = v->value; v->value = 0; called = 1; dtor(p); }
        }
        if (!called) break;
    }
    while (values) { struct value *next = values->next; free(values); values = next; }
}
