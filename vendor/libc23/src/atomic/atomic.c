/* Callable forms of the operations that <stdatomic.h> also defines as macros. */
#include <stdatomic.h>

_Bool (atomic_flag_test_and_set)(volatile atomic_flag *object) {
    return __c11_atomic_exchange(&object->_Value, 1, __ATOMIC_SEQ_CST);
}
_Bool (atomic_flag_test_and_set_explicit)(volatile atomic_flag *object, memory_order order) {
    return __c11_atomic_exchange(&object->_Value, 1, order);
}
void (atomic_flag_clear)(volatile atomic_flag *object) {
    __c11_atomic_store(&object->_Value, 0, __ATOMIC_SEQ_CST);
}
void (atomic_flag_clear_explicit)(volatile atomic_flag *object, memory_order order) {
    __c11_atomic_store(&object->_Value, 0, order);
}
void (atomic_thread_fence)(memory_order order) {
    __c11_atomic_thread_fence(order);
}
void (atomic_signal_fence)(memory_order order) {
    __c11_atomic_signal_fence(order);
}
