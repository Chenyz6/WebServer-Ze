#include "Server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
// https://tool.oschina.net/   http Comparison table

int initListenFd(unsigned short port)
{
	printf("start initListenFd\n");
	// 1.create listen socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0); // AF--Address Family   INET--Internet   SOCK_DGRAM   AF_INET6--ipv6
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
	printf("start epollRun\n");
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
	int size = sizeof(evs) / sizeof(struct epoll_event);
	while (1)
	{
		int nums = epoll_wait(epfd, evs, size, -1); // -1 -- block  0 -- noBlock
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
	printf("start acceptClient\n");
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
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1) perror("epoll_ctl error!");
	return 0;
}

int acceptHttpRequest(int cfd, int epfd)
{
	printf("start acceptHttpRequest\n");
	int len = 0, offset = 0;
	char temp[1024] = { 0 };
	char buf[4096] = { 0 }; // all buf
	while ( (len = recv(cfd, temp, sizeof(temp), 0)) > 0)  // parameter4 -- default:0
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
		char* pt = strstr(buf, "\r\n");
		int reqLen = pt - buf;
		buf[reqLen] = '\0';

		parseRequestLine(buf, cfd);
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

const char* getFileType(const char* name)
{
	printf("start getFileType\n");
	// a.jpg a.mp4 a.html
	// 自右向左查找‘.’字符, 如不存在返回NULL
	const char* dot = strrchr(name, '.');   // from right to left
	if (dot == NULL)
		return "text/plain; charset=utf-8";	// 纯文本
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
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
			<tr>  // row
				<td></td>  // column
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

int sendDir(const char* dirName, int cfd)
{
	printf("start sendDir\n");
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", dirName);
	struct dirent** namelist;
	int num = scandir(dirName, &namelist, NULL, alphasort);
	for (int i = 0; i < num; i++) {
		// file name --> pointer array     struct dirent* temp[]
		char* name = namelist[i]->d_name;
		char subPath[1024] = { 0 };
		sprintf(subPath, "%s/%s", dirName, name);
		struct stat st;
		stat(subPath, &st);
		if (S_ISDIR(st.st_mode)) {
			// a label <a href="">name</a>
			sprintf(buf + strlen(buf), 
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>", 
				name, name, st.st_size);
		}
		else {
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
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


int parseRequestLine(const char* line, int cfd)
{
	printf("start parseRequestLine\n");
	// parsing the request line       get /xxx/1.jpg http/1.1 
	char method[12];
	char path[1024];
	sscanf(line, "%[^ ] %[^ ]", method, path);
	printf("method: %s, path: %s\n", method, path);
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
		file[strlen(file) - 1] = '\0'; // delete end /
	}

	// get file attributes
	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)
	{
		// 404
		sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);  // HTTP Content-type  https://tool.oschina.net/commons   
		sendFile("404.html", cfd);
		perror("stat error!");
		return 0;
	}
	if (S_ISDIR(st.st_mode)) { // is directory
		// send directory to client
		sendHeadMsg(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else { // is file
		// send file content to client
		sendHeadMsg(cfd, 200, "OK", getFileType(file), st.st_size);  // HTTP Content-type  https://tool.oschina.net/commons   
		sendFile(file, cfd);
	}

	return 0;
}

int sendFile(const char* fileName, int cfd)
{
	printf("start sendFile\n");
	printf("send file name: %s\n", fileName);
	// 1.open file
	int fd = open(fileName, O_RDONLY);
	assert(fd > 0);

	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	while (offset < size)
	{
		int ret = sendfile(cfd, fd, &offset, size);	// #include <sys/sendfile.h>    sendfile --> 0 copy
		if (ret == -1) printf("send file error!");
	}


#if 0 // old
	while (1)
	{
		char buf[1024];
		int len = read(fd, buf, sizeof(buf));
		if (len > 0) { 
			send(cfd, buf, len, 0);	//  parameter4 -- default:0  block
			usleep(10); // important
		}
		else if (len == 0) {
			break;
		}
		else {
			perror("read error!");
		}	
	}
#endif
	return 0;
}

int sendHeadMsg(int cfd, int status, const char* describe, const char* type, int length)
{
	printf("start sendHeadMsg\n");
	// status line
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, describe);
	// response header
	sprintf(buf + strlen(buf), "content-type: %s\r\n", type);
	sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);
	// send
	send(cfd, buf, strlen(buf), 0);
	return 0;
}

