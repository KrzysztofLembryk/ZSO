#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

// To check this you need to manually add i.e. some print statement in your bpf 
// program and see if map is being deleted, useful commands:
// - bpftool prog tracelog 
// - bpf_printk("DELETING map entry for pid: %u\n", pid);
int main() 
{
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) perror("open");

    sleep(4);

    return 0;
}
