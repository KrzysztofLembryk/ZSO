#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

// The  dup()  system  call  allocates a new file descriptor that refers to the same open file description as the descriptor oldfd.

 
int main() 
{
    printf("Testing dup...\n");
    int original_fd = open("/dev/null", O_RDONLY);
    if (original_fd < 0) 
    { 
        perror("open for dup"); 
        return 1; 
    }

    for (int i = 0; i < 205; i++) 
    {
        int fd = dup(original_fd);

        if (fd < 0) 
        {
            perror("dup");
        }
    }


    sleep(999999);
    return 0;
}