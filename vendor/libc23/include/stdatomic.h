/* <stdatomic.h> - C23 atomic types and operations for Clang. */
#ifndef _LIBC23_STDATOMIC_H
#define _LIBC23_STDATOMIC_H
#define __STDC_VERSION_STDATOMIC_H__ 202311L

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
typedef _Atomic(bool) atomic_bool;
#else
typedef _Atomic(_Bool) atomic_bool;
#endif

typedef enum memory_order {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

#define ATOMIC_BOOL_LOCK_FREE __CLANG_ATOMIC_BOOL_LOCK_FREE
#define ATOMIC_CHAR_LOCK_FREE __CLANG_ATOMIC_CHAR_LOCK_FREE
#define ATOMIC_CHAR8_T_LOCK_FREE __CLANG_ATOMIC_CHAR8_T_LOCK_FREE
#define ATOMIC_CHAR16_T_LOCK_FREE __CLANG_ATOMIC_CHAR16_T_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE __CLANG_ATOMIC_CHAR32_T_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE __CLANG_ATOMIC_WCHAR_T_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE __CLANG_ATOMIC_SHORT_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE __CLANG_ATOMIC_INT_LOCK_FREE
#define ATOMIC_LONG_LOCK_FREE __CLANG_ATOMIC_LONG_LOCK_FREE
#define ATOMIC_LLONG_LOCK_FREE __CLANG_ATOMIC_LLONG_LOCK_FREE
#define ATOMIC_POINTER_LOCK_FREE __CLANG_ATOMIC_POINTER_LOCK_FREE

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define ATOMIC_VAR_INIT(value) (value)
#endif
#define atomic_init(object, value) __c11_atomic_init((object), (value))
#define kill_dependency(value) (value)
#define atomic_is_lock_free(object) ((void)(object), __c11_atomic_is_lock_free(sizeof(*(object))))

void atomic_thread_fence(memory_order);
void atomic_signal_fence(memory_order);
#define atomic_thread_fence(order) __c11_atomic_thread_fence(order)
#define atomic_signal_fence(order) __c11_atomic_signal_fence(order)

typedef _Atomic(char) atomic_char;
typedef _Atomic(signed char) atomic_schar;
typedef _Atomic(unsigned char) atomic_uchar;
typedef _Atomic(short) atomic_short;
typedef _Atomic(unsigned short) atomic_ushort;
typedef _Atomic(int) atomic_int;
typedef _Atomic(unsigned int) atomic_uint;
typedef _Atomic(long) atomic_long;
typedef _Atomic(unsigned long) atomic_ulong;
typedef _Atomic(long long) atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(unsigned char) atomic_char8_t;
typedef _Atomic(uint_least16_t) atomic_char16_t;
typedef _Atomic(uint_least32_t) atomic_char32_t;
typedef _Atomic(wchar_t) atomic_wchar_t;
typedef _Atomic(int_least8_t) atomic_int_least8_t;
typedef _Atomic(uint_least8_t) atomic_uint_least8_t;
typedef _Atomic(int_least16_t) atomic_int_least16_t;
typedef _Atomic(uint_least16_t) atomic_uint_least16_t;
typedef _Atomic(int_least32_t) atomic_int_least32_t;
typedef _Atomic(uint_least32_t) atomic_uint_least32_t;
typedef _Atomic(int_least64_t) atomic_int_least64_t;
typedef _Atomic(uint_least64_t) atomic_uint_least64_t;
typedef _Atomic(int_fast8_t) atomic_int_fast8_t;
typedef _Atomic(uint_fast8_t) atomic_uint_fast8_t;
typedef _Atomic(int_fast16_t) atomic_int_fast16_t;
typedef _Atomic(uint_fast16_t) atomic_uint_fast16_t;
typedef _Atomic(int_fast32_t) atomic_int_fast32_t;
typedef _Atomic(uint_fast32_t) atomic_uint_fast32_t;
typedef _Atomic(int_fast64_t) atomic_int_fast64_t;
typedef _Atomic(uint_fast64_t) atomic_uint_fast64_t;
typedef _Atomic(intptr_t) atomic_intptr_t;
typedef _Atomic(uintptr_t) atomic_uintptr_t;
typedef _Atomic(size_t) atomic_size_t;
typedef _Atomic(ptrdiff_t) atomic_ptrdiff_t;
typedef _Atomic(intmax_t) atomic_intmax_t;
typedef _Atomic(uintmax_t) atomic_uintmax_t;

#define atomic_store(object, desired) __c11_atomic_store((object), (desired), __ATOMIC_SEQ_CST)
#define atomic_store_explicit(object, desired, order) __c11_atomic_store((object), (desired), (order))
#define atomic_load(object) __c11_atomic_load((object), __ATOMIC_SEQ_CST)
#define atomic_load_explicit(object, order) __c11_atomic_load((object), (order))
#define atomic_exchange(object, desired) __c11_atomic_exchange((object), (desired), __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit(object, desired, order) __c11_atomic_exchange((object), (desired), (order))
#define atomic_compare_exchange_strong(object, expected, desired) __c11_atomic_compare_exchange_strong((object), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit(object, expected, desired, success, failure) __c11_atomic_compare_exchange_strong((object), (expected), (desired), (success), (failure))
#define atomic_compare_exchange_weak(object, expected, desired) __c11_atomic_compare_exchange_weak((object), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak_explicit(object, expected, desired, success, failure) __c11_atomic_compare_exchange_weak((object), (expected), (desired), (success), (failure))
#define atomic_fetch_add(object, operand) __c11_atomic_fetch_add((object), (operand), __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit(object, operand, order) __c11_atomic_fetch_add((object), (operand), (order))
#define atomic_fetch_sub(object, operand) __c11_atomic_fetch_sub((object), (operand), __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(object, operand, order) __c11_atomic_fetch_sub((object), (operand), (order))
#define atomic_fetch_or(object, operand) __c11_atomic_fetch_or((object), (operand), __ATOMIC_SEQ_CST)
#define atomic_fetch_or_explicit(object, operand, order) __c11_atomic_fetch_or((object), (operand), (order))
#define atomic_fetch_xor(object, operand) __c11_atomic_fetch_xor((object), (operand), __ATOMIC_SEQ_CST)
#define atomic_fetch_xor_explicit(object, operand, order) __c11_atomic_fetch_xor((object), (operand), (order))
#define atomic_fetch_and(object, operand) __c11_atomic_fetch_and((object), (operand), __ATOMIC_SEQ_CST)
#define atomic_fetch_and_explicit(object, operand, order) __c11_atomic_fetch_and((object), (operand), (order))

typedef struct atomic_flag { atomic_bool _Value; } atomic_flag;
#define ATOMIC_FLAG_INIT { 0 }
#ifdef __cplusplus
bool atomic_flag_test_and_set(volatile atomic_flag *);
bool atomic_flag_test_and_set_explicit(volatile atomic_flag *, memory_order);
#else
_Bool atomic_flag_test_and_set(volatile atomic_flag *);
_Bool atomic_flag_test_and_set_explicit(volatile atomic_flag *, memory_order);
#endif
void atomic_flag_clear(volatile atomic_flag *);
void atomic_flag_clear_explicit(volatile atomic_flag *, memory_order);
#define atomic_flag_test_and_set(object) __c11_atomic_exchange(&(object)->_Value, 1, __ATOMIC_SEQ_CST)
#define atomic_flag_test_and_set_explicit(object, order) __c11_atomic_exchange(&(object)->_Value, 1, (order))
#define atomic_flag_clear(object) __c11_atomic_store(&(object)->_Value, 0, __ATOMIC_SEQ_CST)
#define atomic_flag_clear_explicit(object, order) __c11_atomic_store(&(object)->_Value, 0, (order))

#ifdef __cplusplus
}
#endif
#endif
