#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>   /* mmap(), mprotect() */

typedef void (*sighandler_t)(int);

sighandler_t make_signal_handler(int signum)
{
    const int COUNT_IDX_IN_CODE = 17;
    const int BUFF_START_IDX_IN_CODE = 23;
    const int BUFF_END_IDX_IN_CODE = 30;
    // ######## SYSCALL TABLE FOR WRITE ########
    // 
    // %rax | system call | %rdi | %rsi | %rdx
    //  1   |  sys_write  |  fd  | *buf | count
    // 
    // #########################################
    // We need to modify byte 15 and set it to our least byte of dynamically 
    // calculated count.
    // We also need to modify bytes 22-25 and set them to our string buf address.
    static uint8_t code[] = {
        // in rdi we have signum - but we ignore it
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,   // mov 1, rax
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,   // mov 1, rdi
        0x48, 0xC7, 0xC2, 0x10, 0x00, 0x00, 0x00,   // mov count, rdx, 
                                                    // count is at 17th byte 
        // 0x48, 0xBE, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
                                                    // mov buf, rsi, addr bytes 22-29
        0x48, 0x89, 0xD0,                           // mov rdx, rax
        // 0x0F, 0x05,                                 // syscall
        0xC3,                       /* ret rax              */
    };

    int n_signum_chars = (int)((ceil(log10(signum))+1));
    printf("n_signum_chars: %d\n", n_signum_chars);

    char *signum_str = (char*)malloc(n_signum_chars);
    sprintf(signum_str, "%d", signum);
    printf("signum_str: %s\n", signum_str);

    uint8_t least_byte = (uint8_t) n_signum_chars;
    printf("least byte: %d\n", least_byte);
    code[COUNT_IDX_IN_CODE] = least_byte;

    unsigned char *ptr_bytes = (unsigned char *)&signum_str;
    // little endian
    printf("sizeof signum_str: %ld\n", sizeof(signum_str));
    // for (size_t i = 0; i < sizeof(signum_str); i++) 
    // {
    //     if (BUFF_START_IDX_IN_CODE + i > BUFF_END_IDX_IN_CODE)
    //     {
    //         printf("BUFF_START_IDX_IN_CODE + i > BUFF_END_IDX_IN_CODE\n");
    //         exit(-1);
    //     }

    //     unsigned char byte = ptr_bytes[i];
    //     code[BUFF_START_IDX_IN_CODE + i] = byte;
    // }

    const size_t len = sizeof(code);

    /* mmap a region for our code */
    void *p = mmap(NULL, len, PROT_READ|PROT_WRITE,  /* No PROT_EXEC */
            MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) 
    {
        fprintf(stderr, "mmap() failed\n");
        exit(2);
    }

    // still not executable
    memcpy(p, code, len);

    if (mprotect(p, len, PROT_EXEC) < 0) {
        fprintf(stderr, "mprotect failed to mark exec-only\n");
        exit(2);
    }

    // func is a pointer to a function taking no args and returning int
    // (int(*)())p - we cast pointer p to a function pointer type int (*)(void)
    int (*func)(int) = (int(*)(int))p;
    // func(signum);
    printf("(dynamic) code returned %d\n", func(signum));
    // printf("(our string) signum: %s\n", signum_str);
}


int main()
{
    make_signal_handler(69);
    pause();
    return 0;
}