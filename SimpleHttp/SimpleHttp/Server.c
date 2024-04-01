#include "Server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
// https://tool.oschina.net/   http Comparison table

int initListenFd(unsigned short port)
{
	// 1.create listen socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0); // AF--Address Family   INET--Internet   SOCK_DGRAM
	if (lfd == -1) perror("socket error!");
	// 2.port multiplexing    2msl
	int opt = 1; // 1 - open port multiplexing
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt));
	if (ret == -1) perror("setsockopt error!");
	// 3.bind
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port); // htons #include <arpa/inet.h>  h--host   n-- network
	addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY -- 0.0.0.0  All IPs of this local computer
	ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1) perror("bind error!");
	// 4.listen
	ret = listen(lfd, 128); // max is 128
	if (ret == -1) perror("listen error!");
	// return fd
	return lfd;
}

int epollRun(int lfd)
{
	// 1.create epoll
	int epfd = epoll_create(1); // 1 need > 0  pointless
	if (epfd == -1) perror("epoll_create error!");
	// 2.lfd to epfd
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN; // EPOLLIN -- read event
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1) perror("epoll_ctl error!");
	// 3.detection
	struct epoll_event evs[1024];
	while (1)
	{
		int size = sizeof(evs) / sizeof(struct epoll_event);
		int nums = epoll_wait(epfd, evs, size, -1); // -1 -- No event keeps blocking
		for (int i = 0; i < nums; i++) {
			int fd = evs[i].data.fd;
			if (fd == lfd) {
				// accept
				acceptClient(lfd, epfd);
			}
			else {
				// receive read
				acceptHttpRequest(fd, epfd);
			}
		}
	}
	return 0;
}

int acceptClient(int lfd, int epfd)
{
	// 1.create
	struct sockaddr_in clientAddr;	
	int clientlen = sizeof(clientAddr);
	char str[32];
	int cfd = accept(lfd, (struct sockaddr*)&clientAddr, &clientlen);
	if (cfd == -1) perror("accept error!");
	printf("Received from %s at PORT %d\n", 
			inet_ntop(AF_INET, &clientAddr.sin_addr, str, sizeof(str)),
			ntohs(clientAddr.sin_port));
	int num = 0;
	printf("cfd %d---client %d\n", lfd, ++num);
	// 2.set non-blocking
	int flag = fcntl(cfd, F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);
	// 3.cfd to epfd
	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET; // EPOLLIN -- read event
	int ret = epoll_ctl(cfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1) perror("epoll_ctl error!");
	return 0;
}

int acceptHttpRequest(int cfd, int epfd)
{
	int len = 0, offset = 0;
	char temp[1024] = { 0 };
	char buf[4096] = { 0 }; // all buf
	while ( (len = recv(cfd, buf, sizeof(buf), 0)) > 0)
	{
		if(offset + len < sizeof(buf)){
			memcpy(buf + offset, temp, len);
		}
		offset += len;
	}
	// whether the data has been received or not
	if (len == -1 && errno == EAGAIN)  // errno = EAGAIN或者errno = EINTR时，并不算错误，此时继续执行循环读取数据就行。
	{
		// parsing the request line

	}
	else if (len == 0) // client close connect
	{
		epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
		close(cfd);
	}
	else {
		perror("recv error!");
	}
	return 0;
}

int parseRequestLine(const char* line, int cfd)
{
	// parsing the request line       get /xxx/1.jpg http/1.1 
	char method[12];
	char path[1024];
	sscanf(line, "%[^ ] %[^ ]", method, path);
	if (strcasecmp(method, "get") != 0) { // strcasecmp -- not distinguishing capitals from lower case letters
		return -1; 
	}
	// get file relative path
	char* file = NULL; 
	if (strcmp(path, "/") == 0) {
		file = "./";
	}
	else {
		file = path + 1;
	}
	// get file attributes
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		// 404
		perror("stat error!");
	}
	if (S_ISDIR(st.st_mode)) { // is directory
		// send directory to client
	}
	else {
		// send file content to client
	}

	return 0;
}

