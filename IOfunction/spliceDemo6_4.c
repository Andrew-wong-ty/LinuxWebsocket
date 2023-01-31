#define _GNU_SOURCE 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>
#include <fcntl.h>


#define BUFFER_SIZE 1024
// 公)124.223.47.124 (内)10.0.4.7 
// http://124.223.47.124:12345/
int main(int argc, char const *argv[])
{
    const char* ip = "10.0.4.7"; // 124.223.47.124 12345
    int port = 12345;
    printf("ip:%s, port:%d\n",ip,port);

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
    int ret = bind(sock, (struct sockaddr *)&address, sizeof(address));
    assert(ret>=0);

    /*prepare to accept*/
    ret = listen(sock, 5);
    assert(ret>=0);

    /*accept client*/
    struct sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int connfd = accept(sock, (struct sockaddr*)&client, &client_addr_len);
    if(connfd<0)
    {
        printf("connection failed\n");
    }
    else{
        printf("connection succeed\n");
        int pipefd[2];  
        ret = pipe(pipefd); //创建管道  1是写段, 接受数据写入管道, 0是读端, 读取1端写入的数据
        assert(ret!=-1);
        // 从connd到管道 in->out (conndf->pip)
        ret = splice(connfd, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret!=-1);
        ret = splice(pipefd[0], NULL, connfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
        assert(ret!=-1);
        close(connfd);

    }

    close(sock);
    return 0;
}
