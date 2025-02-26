#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>


int main(void)
{
	int fd;	
	fd = open("/dev/scull_pipe0", O_RDWR);
	if (fd < 0) {
		printf("failed to open scull_pipe0: %s!\n", strerror(errno));
		return -1;
	}
	
	const char *msg = "test fasync!";
    int len = strlen(msg);

	int ret = write(fd, msg, len);
	if(ret < 0){
        printf("failed to write data: %s!\n", strerror(errno));
	}

	close(fd);
	return 0;
}