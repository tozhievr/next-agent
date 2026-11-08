/* Process entry for the Linux backend: _start, TLS for the main thread, argv/environ.
 * Statically linked, no dynamic loader and no host libc. */
#include "../pal.h"
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  xor %ebp, %ebp\n"
        "  mov %rsp, %rdi\n"
        "  and $-16, %rsp\n"
        "  call __libc_linux_start\n"
        "  ud2\n");
#endif

const unsigned char *__pal_tls_image;
size_t __pal_tls_filesz, __pal_tls_memsz, __pal_tls_align = 1;

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} Phdr;

void *__pal_tls_init(char *area, size_t len);
size_t __pal_tls_area_size(void);

#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define PT_TLS 7

void __libc_linux_start(long *sp) {
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    char **envp = argv + argc + 1;
    char **e = envp;
    while (*e) e++;
    uint64_t *auxv = (uint64_t *)(e + 1);
    const Phdr *ph = 0;
    uint64_t phnum = 0, phent = sizeof(Phdr);
    for (; auxv[0]; auxv += 2) {
        if (auxv[0] == AT_PHDR) ph = (const Phdr *)auxv[1];
        else if (auxv[0] == AT_PHNUM) phnum = auxv[1];
        else if (auxv[0] == AT_PHENT) phent = auxv[1];
    }
    for (uint64_t i = 0; ph && i < phnum; i++) {
        const Phdr *p = (const Phdr *)((const char *)ph + i * phent);
        if (p->type == PT_TLS) {
            __pal_tls_image = (const unsigned char *)p->vaddr;
            __pal_tls_filesz = p->filesz;
            __pal_tls_memsz = p->memsz;
            __pal_tls_align = p->align ? p->align : 1;
        }
    }
    size_t ps = 4096, need = (__pal_tls_area_size() + ps - 1) & ~(ps - 1);
    char *area = pal_mem_map(need);
    if (!area) pal_exit(127);
    void *tp = __pal_tls_init(area, need);
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(158L), "D"(0x1002L), "S"(tp) : "rcx", "r11", "memory"); /* ARCH_SET_FS */
    if (r != 0) pal_exit(127);
    pal_environ = envp;
    __libc_start_main(argc, argv, envp);
}
