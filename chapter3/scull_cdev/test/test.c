#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
 
 
#define BUFSIZE  64
#define COUNT 100

char buf[BUFSIZE];
 
int main(void)
{
	int fd;
	for (int i = 0; i < BUFSIZE; i++) {
		buf[i] = (char)i + '!';
	}
	
	fd = open("/dev/scull0", O_WRONLY);
	if (fd< 0) {
		printf("failed to open scull0, fd is %d!\n", fd);
		exit(-1);
	}
  
	for (int i = 0; i < COUNT; i++) {
		int len = write(fd, buf, BUFSIZE);
		if (len == -1) {
			perror("failed to write data to scull0!");
		} 
		else {
			printf("scull0: write len = %d\n", len);		
		}
	}
 
	close(fd);
	
	fd = open("/dev/scull0", O_RDONLY);
	if (fd == -1) {
		printf("failed to open scull0, fd is %d!\n", fd);
		exit(-1);
	}
 
	for (int i = 0; i < BUFSIZE; i++) {
		memset(buf, 0, BUFSIZE);
		int len = read(fd, buf, BUFSIZE - i);
		if (len == -1) {
			perror("failed to read data from scull0!");
		} 
		else {
			printf("scull0: read[%d] len = %d \ndata = ", i + 1, len);
			for (int j = 0; j < BUFSIZE; j++)
				printf("%c", buf[j]);
			printf("\n");
		}
	}
 
	close(fd);
	
	return 0;
}