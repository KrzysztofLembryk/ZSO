// tests/test_fds_socket.c
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    // Create 200 sockets to trigger the fd limit
    for (int i = 0; i < 120; i++) {
        // AF_INET for IPv4, SOCK_STREAM for TCP
        // You could also use AF_UNIX for local sockets
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        
        if (fd < 0) {
            perror("socket");
        }
    }
    
    printf("Finished opening sockets. Sleeping...\n");
    sleep(999999);
    
    return 0;
}