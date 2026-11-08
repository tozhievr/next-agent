/* setjmp / longjmp for aarch64 (AAPCS64, also Windows ARM64).
 * Layout (bytes): 0..95 x19..x30 (x29,x30 at 80,88), 96 sp, 104..167 d8..d15. */
#if defined(__aarch64__)
#if defined(_WIN32)
#define SJ_DEF(n) ".def " #n "; .scl 2; .type 32; .endef\n"
#else
#define SJ_DEF(n) ".type " #n ",%function\n"
#endif
__asm__(
    ".text\n"
    SJ_DEF(setjmp)
    ".globl setjmp\n"
    ".p2align 4\n"
    "setjmp:\n"
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x2, sp\n"
    "  str x2, [x0, #96]\n"
    "  stp d8, d9, [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    "  mov w0, #0\n"
    "  ret\n"
    SJ_DEF(longjmp)
    ".globl longjmp\n"
    ".p2align 4\n"
    "longjmp:\n"
    "  ldp x19, x20, [x0, #0]\n"
    "  ldp x21, x22, [x0, #16]\n"
    "  ldp x23, x24, [x0, #32]\n"
    "  ldp x25, x26, [x0, #48]\n"
    "  ldp x27, x28, [x0, #64]\n"
    "  ldp x29, x30, [x0, #80]\n"
    "  ldr x2, [x0, #96]\n"
    "  mov sp, x2\n"
    "  ldp d8, d9, [x0, #104]\n"
    "  ldp d10, d11, [x0, #120]\n"
    "  ldp d12, d13, [x0, #136]\n"
    "  ldp d14, d15, [x0, #152]\n"
    "  cmp w1, #0\n"
    "  csinc w0, w1, wzr, ne\n"
    "  ret\n");
#endif
