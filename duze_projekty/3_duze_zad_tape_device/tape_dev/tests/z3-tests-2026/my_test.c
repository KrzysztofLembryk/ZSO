#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <stdint.h>

#define TAPEDEV_IOCTL_GET_INFO             _IOR('~', 0, struct tapedev_sect_info)

struct tapedev_sect_info {
    uint32_t tapes;
    uint32_t tape_type;
    uint32_t current_tape;
};

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("usage: %s [path_to_disk]\n", argv[0]);
        return -1;
    }

}