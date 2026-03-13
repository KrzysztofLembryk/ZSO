#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>   /* mmap(), mprotect() */

typedef void (*sighandler_t)(int);

sighandler_t make_signal_handler(int signum)
{
    const size_t COUNT_IDX_IN_CODE = 25;
    const size_t BUFF_START_IDX_IN_CODE = 31;
    const size_t BUFF_END_IDX_IN_CODE = 38;
    // ######## SYSCALL TABLE FOR WRITE ########
    // 
    // %rax | system call | %rdi | %rsi | %rdx
    //  1   |  sys_write  |  fd  | *buf | count
    // 
    // #########################################
    static uint8_t code[] = {
        // in rdi we have signum - but we ignore it
        0x55,                                       // push rbp
        0x48, 0x89, 0xEC,                           // mov rsp,rbp
        0x48, 0x83, 0xEC, 0x10,                     // sub 0x10,rsp
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,   // mov 1, rax
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,   // mov 1, rdi
        0x48, 0xC7, 0xC2, 0x10, 0x00, 0x00, 0x00,   // mov count, rdx, 
        0x48, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                    // movabs buf, rsi
        0x0F, 0x05,                                 // syscall
        0x90,                                       // nop  
        0xC9,                                       // leave
        0xC3,                                       // ret
    };

    // snprintf returns number of characters that would be saved if buffer was big
    // enough
    int n_signum_digits = snprintf(NULL, 0, "%d", signum);
    char *signum_str = (char*)malloc(n_signum_digits);

    sprintf(signum_str, "%d", signum);

    uint8_t least_byte = (uint8_t) (n_signum_digits);
    uint8_t *ptr_bytes = (uint8_t *)&signum_str;

    code[COUNT_IDX_IN_CODE] = least_byte;
    // little endian, ptr has 8 bytes, we allocated memory so even after function
    // returns, pointer is still valid, thus we write it to our code in place that
    // moves this pointer to RSI
    for (size_t i = 0; i < sizeof(signum_str); i++) 
    {
        if (BUFF_START_IDX_IN_CODE + i > BUFF_END_IDX_IN_CODE)
        {
            printf("BUFF_START_IDX_IN_CODE + i > BUFF_END_IDX_IN_CODE\n");
            exit(-1);
        }

        uint8_t byte = ptr_bytes[i];
        code[BUFF_START_IDX_IN_CODE + i] = byte;
    }

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
    void (*func)(int) = (void(*)(int))p;
    // func(signum);

    return signal(signum, func);
}


int main()
{
    make_signal_handler(69);
    pause();
    return 0;
}