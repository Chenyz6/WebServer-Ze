#include "server.h"
#include <arpa/inet.h>   // AF_INET
#include <sys/epoll.h>
#include <stdio.h>       // NULL
#include <fcntl.h>       // fcntl()
#include <errno.h>       // errno EAGAIN
#include <strings.h>     // strcasemap()
#include <string.h>
#include <sys/stat.h>    // stat()
#include <assert.h>
#include <sys/sendfile.h> // sendfile()
#include <dirent.h>      // scandir()
#include <unistd.h>      // close()
#include <stdlib.h>      // free()
#include <pthread.h>
#include <ctype.h>


// 初始化监听套接字
int InitListenFd(unsigned short port)
{
    // 1.创建监听fd
    // AF_INET6(IPV6)    SOCK_DGRAM--UDP    0 --> 自动选择type类型对应的默认协议
    int lfd = socket(AF_INET, SOCK_STREAM, 0);  
    if(lfd == -1){
        perror("socket error!");
        return -1;
    }
    // 2.设置端口复用
    int opt = 1; // 1 --> 设置可以复用
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // int setsockopt(int sockFd, int level, int optname, const void *optval, socklen_t optlen);
    // level一般设成SOL_SOCKET以存取socket层     SO_REUSEADDR  允许重用本地地址和端口
    if(ret == -1){
        perror("setsockopt error!");
        return -1;
    }
    // 3.绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    if(ret == -1){
        perror("bind error!");
        return -1;
    }
    // 4.监听
    ret = listen(lfd, 128);   // 一次性能够和多少个客户端进行连接
    if(ret == -1){
        perror("listen error!");
        return -1;
    }
    // 5.返回fd
    return lfd;
}

// 启动epoll
int EpollRun(int lfd)
{
    // 1.创建epoll实例
    // size参数只是告诉内核这个 epoll对象会处理的事件大致数目，而不是能够处理的事件的最大个数
    // 在 Linux最新的一些内核版本的实现中，这个 size参数没有任何意义
    int epfd = epoll_create(1);
    if(epfd == -1){
        perror("epoll_create error!");
        return -1;
    }
    // 2.lfd上树
    // 1)成员 events：EPOLLIN / EPOLLOUT / EPOLLERR   2)成员 data： 联合体（共用体）：
    // typedef union epoll_data {
    //     void *ptr;
    //     int fd;  // 对应监听事件的 fd
    //     __uint32_t u32;
    //     __uint64_t u64;
    // } epoll_data_t;	
    struct epoll_event ev;
    ev.data.fd = lfd;
    ev.events = EPOLLIN;
    // EPOLL_CTL_ADD 添加fd到 监听红黑树
    // EPOLL_CTL_MOD 修改fd在 监听红黑树上的监听事件
    // EPOLL_CTL_DEL 将一个fd 从监听红黑树上摘下（取消监听）
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(epfd == -1){
        perror("epoll_ctl error!");
        return -1;
    }
    // 3.检测
    struct epoll_event evs[1024];
    while(1){
        // timeout  -1-->阻塞   0-->不阻塞   >0-->超时时间(毫秒）
        int size = sizeof(evs) / sizeof(struct epoll_event);
        int num = epoll_wait(epfd, evs, sizeof(evs), -1);
        for(int i = 0; i < num; i++){
            int fd = evs[i].data.fd;
            if(fd == lfd){ // 如果是监听描述符 建立新连接
                // 建立新连接 accept
                AcceptClient(lfd, epfd);
            }else{
                // 主要是接收对端的数据
                AcceptHttpRequest(fd, epfd);
            }
        }
    }

    return 0;
}

// 和客户端建立连接
int AcceptClient(int lfd, int epfd)
{
    // 1.接受连接
    int cfd = accept(lfd, NULL, NULL);
    if(cfd == -1){
        perror("accept error!");
        return -1;
    }

    // 2.设置非阻塞 边缘模式
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    // 3.将cfd添加到epoll模型
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = cfd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1){
        perror("epoll_ctl error!");
        return -1;
    }
    return 0;
}

// 接受http请求消息
int AcceptHttpRequest(int cfd, int epfd)
{
    char buffer[4096] = {0};
    char temp[1024] = {0};
    int offset = 0, len = 0;
    while( (len = recv(cfd, temp, sizeof(temp), 0)) > 0 ){  // recv第四个参数一般置0
        if(offset + len < sizeof(buffer)){
            memcpy(buffer + offset, temp, len);
        }
        offset += len;
    }

    // 判断数据是否接受完毕
    if(len == -1 && errno == EAGAIN){ 
        // 解析请求行
        char * pt = strstr(buffer, "\r\n");  // 大字符串搜索子字符串 返回找到的第一个字符位置
        int reqLen = pt - buffer;
        buffer[reqLen] = '\0';
        ParseRequestLint(buffer, cfd);
    }else if(len == 0){
        // 客户端断开连接
        epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL); // 删除指定为NULL
        close(cfd);
    }else{
        perror("recv error!");
        return -1;
    }
    return 0;
}

// 解析请求行 get /xxx/1.jpg http/1.1
int ParseRequestLint(const char *line, int cfd)
{
    char method[12];
    char path[1024];
    sscanf(line, "%[^ ] %[^ ]", method, path);
    if(strcasecmp(method, "get") != 0){ // strcasemap 比较时不区分大小写
        return -1;
    }
    decodeMsg(path, path);
    // 处理客户端请求的静态资源(目录或文件)
    char * file = NULL;
    if(strcmp(path, "/") == 0){
        file = "./";
    }else{
        file = path + 1;
    }

    // 获取文件属性
    struct stat st; 
    int ret = stat(file, &st);
    if(ret == -1){
        // 文件不存在 恢复404
        SendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1); // 对照表 https://tool.oschina.net/commons
        SendFile("404.html", cfd);
        return 0;
    }
    // 判断文件类型
    if(S_ISDIR(st.st_mode)){  // 返回1为目录
        // 把这个目录内容发送客户端
        // 目录遍历方法  https://subingwen.cn/linux/directory/
        SendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
        SendDir(file, cfd);
    }else{
        // 把文件内容发送给客户端
        SendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size); // 对照表 https://tool.oschina.net/commons
        SendFile(file, cfd);
    }
    return 0;
}

// 发送文件
int SendFile(const char *fileName, int cfd)
{
    // 1. 打开文件
    int fd = open(fileName, O_RDONLY);
    if (fd <= 0) {
        perror("open failed!");
        printf("errno: %d\n", errno);
    }
    assert(fd > 0);
#if 0 
    while (1)
    {
        char buf[1024];
        int len = read(fd, buf, sizeof buf);
        if (len > 0)
        {
            send(cfd, buf, len, 0);
            usleep(10); // 这非常重要
        }
        else if (len == 0)
        {
            break;
        }
        else
        {
            perror("read");
        }
    }
#else
    off_t offset = 0;
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    while (offset < size)
    {
        int ret = sendfile(cfd, fd, &offset, size - offset);
        printf("ret value: %d\n", ret);
        if (ret == -1 && errno == EAGAIN)
        {
            printf("没数据...\n");
        }
    }
#endif
    close(fd);
    return 0;
}

// 发送响应头(状态行 + 响应头)
int SendHeadMsg(int cfd, int status, const char *descr, const char *type, int length)
{
    // 状态行
    char buf[4096];
    sprintf(buf, "http/1.1 %d %s\r\n", status, descr);
    // 响应头
    sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
    sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);

    send(cfd, buf, strlen(buf), 0);
    return 0;
}

// 通过文件类型 找到content_type
const char* getFileType(const char* name)
{
    // a.jpg a.mp4 a.html
    // 自右向左查找‘.’字符, 如不存在返回NULL
    const char* dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";	// 纯文本
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image /gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

/*
<html>
    <head>
        <title>test</title>
    </head>
    <body>
        <table>
            <tr>   行
                <td></td>    列
                <td></td>
            </tr>
            <tr>
                <td></td>
                <td></td>
            </tr>
        </table>
    </body>
</html>
*/

// 发送目录
int SendDir(const char *dirName, int cfd)
{
    char buf[4096] = { 0 };
    sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
    struct dirent** namelist;
    int num = scandir(dirName, &namelist, NULL, NULL);
    // int num = scandir(dirName, &namelist, NULL, alphasort());
    for (int i = 0; i < num; ++i)
    {
        // 取出文件名 namelist 指向的是一个指针数组 struct dirent* tmp[]
        char* name = namelist[i]->d_name;
        struct stat st;
        char subPath[1024] = { 0 };
        sprintf(subPath, "%s/%s", dirName, name);
        stat(subPath, &st);
        if (S_ISDIR(st.st_mode))
        {
            // a标签 <a href="">name</a>
            sprintf(buf + strlen(buf), 
                "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", 
                name, name, st.st_size);
        }
        else
        {
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                name, name, st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    free(namelist);
    return 0;
}


// 将字符转换为整形数
int hexToDec(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

// 解码
// to 存储解码之后的数据, 传出参数, from被解码的数据, 传入参数
void decodeMsg(char* to, char* from)
{
    for (; *from != '\0'; ++to, ++from)
    {
        // isxdigit -> 判断字符是不是16进制格式, 取值在 0-f
        // Linux%E5%86%85%E6%A0%B8.jpg
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
        {
            // 将16进制的数 -> 十进制 将这个数值赋值给了字符 int -> char
            // B2 == 178
            // 将3个字符, 变成了一个字符, 这个字符就是原始数据
            *to = hexToDec(from[1]) * 16 + hexToDec(from[2]);

            // 跳过 from[1] 和 from[2] 因此在当前循环中已经处理过了
            from += 2;
        }
        else
        {
            // 字符拷贝, 赋值
            *to = *from;
        }

    }
    *to = '\0';
}