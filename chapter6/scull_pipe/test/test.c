#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>


int set_socket_nonblocking(int fd) {
    // 获取当前文件描述符的标志
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }

    // 设置为非阻塞模式
    flags |= O_NONBLOCK;  // 将 O_NONBLOCK 标志添加到现有标志中

    // 设置文件描述符的新标志
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}
  
int main(void)
{
	int fd;	
	fd = open("/dev/scull_pipe0", O_RDWR);
	if (fd < 0) {
		printf("failed to open scull_pipe0: %s!\n", strerror(errno));
		exit(-1);
	}

	//设置非阻塞IO
	// if(set_socket_nonblocking(fd)){
	// 	exit(-1);
	// }
	
	int buf[128] = {0};
	int ret = 0;

	/*
		此时 scull_pipe0 设备里还没有数据，但是套接字设置了 O_NONBLOCK
	    所以返回值 ret = -1, errno = EAGAIN, 也就是会打印下面这段话 [read] there is no data to read!
	*/

	printf("read before!\n");

	ret = read(fd, buf, sizeof(buf));
	if(ret < 0){
		if(errno == EAGAIN){
			printf("[read] there is no data to read!\n");
		}
	}

	printf("read end!\n");
	close(fd);

	return 0;
}