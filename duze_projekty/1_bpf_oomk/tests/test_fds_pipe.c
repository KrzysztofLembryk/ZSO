#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    for (int i = 0; i < 100; i++) 
    { // Each pipe gives 2 fds
        int fds[2];
        if (pipe(fds) < 0) perror("pipe");
    }
    sleep(999999);
    return 0;
}