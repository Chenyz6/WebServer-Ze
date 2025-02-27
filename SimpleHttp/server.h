#pragma once

// 初始化监听套接字
int InitListenFd(unsigned short port);

// 启动epoll
int EpollRun(int lfd);

// 和客户端建立连接
int AcceptClient(int lfd, int epfd);

// 接受http请求消息
int AcceptHttpRequest(int cfd, int epfd);

// 解析请求行
int ParseRequestLint(const char* line, int cfd);

// 发送文件
int SendFile(const char* fileName, int cfd);

// 发送响应头(状态行 + 响应头)
int SendHeadMsg(int cfd, int status, const char * descr, const char * type, int length);

// 通过文件类型 找到content_type
const char* getFileType(const char* name);

// 发送目录
int SendDir(const char* dirName, int cfd);

// 将字符转换为整形数
int hexToDec(char c);

// 解码
// to 存储解码之后的数据, 传出参数, from被解码的数据, 传入参数
void decodeMsg(char* to, char* from);