#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/fs.h>
#include <fcntl.h>

int main(int argc, char** argv) {
	if (argc != 2) {
		printf("usage: %s [path_to_adlerdev]\n", argv[0]);
		return -1;
	}

	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("failed to open file");
		return -1;
	}

	while (true) {
		char buf[128];

		int bytes = read(STDIN_FILENO, &buf, 128);
		if (bytes < 0) {
			perror("failed to read");
			return -1;
		} else if (bytes == 0) {
			break;
		}

		int wr = write(fd, buf, bytes);
		if (wr < 0) {
			perror("failed to write");
			return -1;
		}
	}

	int sum = 0;
	read(fd, &sum, 4);
	printf("result: %x\n", sum);

	return 0;
}
