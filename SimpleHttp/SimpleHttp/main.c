#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "Server.h"

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Please import:  ./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);
    chdir(argv[2]); // change directory
    // init listen socket
    int lfd = initListenFd(port);
    // start server
    epollRun(lfd);
    return 0;
}