/* assert failure reporting: no stdio, writes straight through the PAL. */
#include "libc.h"

_Noreturn void abort(void);

static void put(const char *s) {
    size_t n = 0, w;
    pal_file_t f = pal_std_file(2);
    while (s[n]) n++;
    while (n && pal_write(f, s, n, &w) == 0 && w) { s += w; n -= w; }
}

_Noreturn void __libc_assert_fail(const char *expr, const char *func, const char *file, int line) {
    char num[16]; int i = 16;
    unsigned u = line < 0 ? 0u : (unsigned)line;
    num[--i] = 0;
    do { num[--i] = (char)('0' + u % 10); u /= 10; } while (u);
    put("Assertion failed: ");
    put(expr);
    put(", function ");
    put(func);
    put(", file ");
    put(file);
    put(", line ");
    put(num + i);
    put(".\n");
    abort();
}
