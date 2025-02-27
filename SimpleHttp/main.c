#include <stdio.h>
#include <unistd.h> // chdir()
#include <stdlib.h> // atoi()
#include "server.h"

int main(int argc, char * argv[])
{
    if(argc < 3){
        printf("./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);
    // 切换服务器工作路径
    chdir(argv[2]);
    // 初始化监听套接字
    int lfd = InitListenFd(port);  // 0-65535  最好不要5000以下
    // 启动服务器
    EpollRun(lfd);
    return 0;
}