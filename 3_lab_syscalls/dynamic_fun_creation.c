#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>   /* mmap(), mprotect() */

static uint8_t code[] = {
    0xB8,0xFF,0x00,0x00,0x00,   /* mov  eax,0xFF    */
    0xC3,                       /* ret              */
};

int main(void)
{
    const size_t len = sizeof(code);

    /* mmap a region for our code */
    void *p = mmap(NULL, len, PROT_READ|PROT_WRITE,  /* No PROT_EXEC */
            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p==MAP_FAILED) {
        fprintf(stderr, "mmap() failed\n");
        return 2;
    }

    /* Copy it in (still not executable) */
    memcpy(p, code, len);

    /* Now make it execute-only */
    if (mprotect(p, len, PROT_EXEC) < 0) {
        fprintf(stderr, "mprotect failed to mark exec-only\n");
        return 2;
    }

    /* Go! */
    int (*func)(void) = (int(*)())p;
    printf("(dynamic) code returned %d\n", func());

    pause();
    return 0;
}