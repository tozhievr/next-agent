/* <assert.h> - C23 7.2 (libc23).  May be included several times. */
#undef assert

#ifdef __cplusplus
extern "C" {
#endif
#ifndef _LIBC23_ASSERT_DECL
#define _LIBC23_ASSERT_DECL
#define __STDC_VERSION_ASSERT_H__ 202311L
__attribute__((noreturn)) void __libc_assert_fail(const char *__expr, const char *__func,
                                                  const char *__file, int __line);
#endif
#ifdef __cplusplus
}
#endif

#ifdef NDEBUG
#define assert(...) ((void)0)
#else
#define assert(...) \
    ((__VA_ARGS__) ? (void)0 : __libc_assert_fail(#__VA_ARGS__, __func__, __FILE__, __LINE__))
#endif
