// tests/test_fds_openat.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    for (int i = 0; i < 200; i++) {
        // AT_FDCWD makes openat behave like open
        int fd = openat(AT_FDCWD, "/dev/null", O_RDONLY);
        if (fd < 0) {
            perror("openat");
        }
    }
    sleep(999999);
    return 0;
}