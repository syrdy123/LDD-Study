#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

#define SCULL_IOC_MAGIC  'k'

#define SCULL_IOCRESET    _IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC,  1, int)
#define SCULL_IOCSQSET    _IOW(SCULL_IOC_MAGIC,  2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC,   3)
#define SCULL_IOCTQSET    _IO(SCULL_IOC_MAGIC,   4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC,  5, int)
#define SCULL_IOCGQSET    _IOR(SCULL_IOC_MAGIC,  6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC,   7)
#define SCULL_IOCQQSET    _IO(SCULL_IOC_MAGIC,   8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET    _IOWR(SCULL_IOC_MAGIC,10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC,  11)
#define SCULL_IOCHQSET    _IO(SCULL_IOC_MAGIC,  12)
#define SCULL_P_IOCTSIZE _IO(SCULL_IOC_MAGIC,   13)
#define SCULL_P_IOCQSIZE _IO(SCULL_IOC_MAGIC,   14)
 
 
 
int main(void)
{
	int fd;	
	fd = open("/dev/scull_ioctl0", O_WRONLY);
	if (fd < 0) {
		printf("failed to open scull_ioctl0: %s!\n", strerror(errno));
		exit(-1);
	}

	//用 scull_quantum 做测试
	
	//get
	int quantum = 0;
	if(ioctl(fd, SCULL_IOCGQUANTUM, &quantum)){
		printf("[SCULL_IOCGQUANTUM] cannot get scull_quantum: %s!\n", strerror(errno));
	}
	else{
		printf("[SCULL_IOCGQUANTUM] get scull_quantum successfully, quantum is %d\n", quantum);
	}

	//set
	quantum = 200;
	if(ioctl(fd, SCULL_IOCTQUANTUM, quantum)){
		printf("[SCULL_IOCTQUANTUM] cannot set scull_quantum: %s!\n", strerror(errno));
	}
	else{
		printf("[SCULL_IOCTQUANTUM] set scull_quantum successfully!\n");
	}

	quantum = 300;
	if(ioctl(fd, SCULL_IOCSQUANTUM, &quantum)){
		printf("[SCULL_IOCSQUANTUM] cannot set scull_quantum: %s!\n", strerror(errno));
	}
	else{
		printf("[SCULL_IOCSQUANTUM] set scull_quantum successfully!\n");
	}

	//get
	quantum = ioctl(fd, SCULL_IOCQQUANTUM, quantum);
	printf("[SCULL_IOCQQUANTUM] get scull_quantum successfully, quantum is %d\n", quantum);

	//exchange
	quantum = 123;
	quantum = ioctl(fd, SCULL_IOCHQUANTUM, quantum);
	printf("[SCULL_IOCHQUANTUM] exchange scull_quantum successfully, quantum is %d!\n", quantum);

	//get
	quantum = ioctl(fd, SCULL_IOCQQUANTUM, quantum);
	printf("[SCULL_IOCQQUANTUM] get scull_quantum successfully, quantum is %d\n", quantum);
	

	quantum = 555;
	if(ioctl(fd, SCULL_IOCXQUANTUM, &quantum)){
		printf("[SCULL_IOCXQUANTUM] cannot exchange scull_quantum: %s!\n", strerror(errno));
	}
	else{
		printf("[SCULL_IOCXQUANTUM] exchange scull_quantum successfully, quantum is %d!\n", quantum);
	}

	//get
	quantum = ioctl(fd, SCULL_IOCQQUANTUM, quantum);
	printf("[SCULL_IOCQQUANTUM] get scull_quantum successfully, quantum is %d\n", quantum);
}