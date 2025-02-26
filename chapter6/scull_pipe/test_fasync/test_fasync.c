#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>


#define DEVICE_PATH "/dev/scull_pipe0"

static bool signal_recieved = false;
static char buf[128] = {0};

// 信号处理函数
void handle_signal(int sig) {
    if (sig == SIGIO) {
        printf("Received SIGIO signal!\n");
        signal_recieved = true;
    }
}

int main() {
    int fd = open(DEVICE_PATH, O_RDWR);  // 打开设备文件
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // 设置 FASYNC 标志
    if (fcntl(fd, F_SETFL, O_ASYNC) < 0) {
        perror("fcntl");
        close(fd);
        return -1;
    }

    // 注册信号处理函数
    signal(SIGIO, handle_signal);

    // 设置进程为异步信号接收者
    fcntl(fd, F_SETOWN, getpid());  // 将信号发送给当前进程

    // 模拟一个等待的操作，等待设备信号
    printf("Waiting for signal...\n");
    while (1) {
        if(signal_recieved){
            int ret = read(fd, buf, sizeof(buf));
            if(ret > 0){
                printf("read msg from driver: %s!\n", buf);
            }
            else{
                printf("failed to read msg: %s!\n", strerror(errno));
            }

            break;
        }
    }

    close(fd);
    return 0;
}
