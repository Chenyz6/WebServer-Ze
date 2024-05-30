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

// send file
int sendFile(const char* fileName, int cfd);

// send response header (status line and Response Header)
int sendHeadMsg(int cfd, int status, const char* describe, const char* type, int length);

// get HTTP Content-type
const char* getFileType(const char* name);

// send directory
int sendDir(const char* dirName, int cfd);