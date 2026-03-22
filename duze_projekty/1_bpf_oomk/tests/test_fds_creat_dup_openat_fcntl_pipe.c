// tests/test_fds_creat_dup_openat_fcntl_pipe.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

int main() {

    for (int i = 0; i < 20; i++) {
        int fd = creat("/dev/null", 0666);
        if (fd < 0) perror("creat");
    }

    for (int i = 0; i < 10; i++) {
        // AT_FDCWD makes openat behave like open
        int fd = openat(AT_FDCWD, "/dev/null", O_RDONLY);
        if (fd < 0) {
            perror("openat");
        }
    }

    for (int i = 0; i < 10; i++) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) perror("open");
    }

    for (int i = 0; i < 10; i++) 
    { // Each pipe gives 2 fds
        int fds[2];
        if (pipe(fds) < 0) perror("pipe");
    }

    int original_fd = open("/dev/null", O_RDONLY);
    if (original_fd < 0) 
    { 
        perror("open for dup"); 
        return 1; 
    }

    for (int i = 0; i < 20; i++) 
    {
        int fd = dup(original_fd);

        if (fd < 0) 
        {
            perror("dup");
        }
    }

    int base = open("/dev/null", O_RDONLY);
    if (base < 0) 
    { 
        perror("open for fcntl"); 
        return 1; 
    }
    for (int i = 0; i < 30; i++) {
        int fd = fcntl(base, F_DUPFD, 0);
        if (fd < 0) perror("fcntl");
    }
    
    sleep(999999);

    return 0;
}