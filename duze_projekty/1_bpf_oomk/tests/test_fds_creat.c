#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() 
{
    for (int i = 0; i < 200; i++) {
        int fd = creat("/dev/null", 0666);
        if (fd < 0) perror("creat");
    }
    sleep(999999);
    return 0;
}