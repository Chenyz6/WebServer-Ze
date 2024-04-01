#pragma once

// init listen fd
int initListenFd(unsigned short port); // 0-65535

// start epoll
int epollRun(int lfd);

// connect client
int acceptClient(int lfd, int epfd);

// receive http
int acceptHttpRequest(int cfd, int epfd);

// parsing the request line
int parseRequestLine(const char * line, int cfd);