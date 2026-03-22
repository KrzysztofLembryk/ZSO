#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int base = open("/dev/null", O_RDONLY);
    if (base < 0) 
    { 
        perror("open for fcntl"); 
        return 1; 
    }
    for (int i = 0; i < 200; i++) {
        int fd = fcntl(base, F_DUPFD, 0);
        if (fd < 0) perror("fcntl");
    }

    sleep(999999);
    return 0;
}