#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "rgbled2.h"

int main(int argc, char *argv[])
{
	int fd;
	COLOR test;

	/* Open device file */
	if ((fd = open("/dev/rgbled", O_WRONLY, 0222)) <= 0) {
		perror("E OPEN");
		return -1;
	}

	/* Write color values to driver */
	if (argc == 4) { 
		sscanf(argv[1], "%d", &test.r);
		sscanf(argv[2], "%d", &test.g);
		sscanf(argv[3], "%d", &test.b);
		if (ioctl(fd, MYWRITE, &test)){
			perror("E WRITE");
			close(fd);
			return -2;
		}
	}

	close(fd);
	return 0;
}
