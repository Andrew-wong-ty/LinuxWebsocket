#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#define BUFFER_SIZE 1024
static const char* status_line[2] = { "200 OK", "500 Internal server error" };
// 公)124.223.47.124 (内)10.0.4.7 
// http://124.223.47.124:12345/
int main(int argc, char const *argv[])
{
    const char* ip = "10.0.4.7"; // 124.223.47.124 12345
    int port = 12345;
    const char* filename = "/work/SocketLearn/data/test.txt";
    bool valid;
    printf("ip:%s, port:%d\n",ip,port);

    /*打开文件*/
    int filefd = open(filename, O_RDONLY);
    assert(filefd>0);
    struct stat stat_buf;
    fstat(filefd, &stat_buf);

    /*创建socket*/
    int sock = socket(PF_INET,SOCK_STREAM, 0);
    assert(sock>=0);

    /*创建socket address*/
    struct sockaddr_in address;
    memset(&address, '\0', sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(sock, ip, &address.sin_addr);
    address.sin_port = htons(port);

    /*bind*/
    int ret = bind(sock, (sockaddr *)&address, sizeof(address));
    assert(ret>=0);

    /*prepare to accept*/
    ret = listen(sock, 5);
    assert(ret>=0);

    /*accept client*/
    struct sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int connfd = accept(sock, (sockaddr*)&client, &client_addr_len);
    if(connfd<0)
    {
        printf("connection failed\n");
    }
    else{
        printf("connection succeed\n");
        sendfile(connfd,filefd, NULL, stat_buf.st_size ); // 直接传输文件
        close(connfd);
        close(filefd);
    }

    close(sock);
    return 0;
}
