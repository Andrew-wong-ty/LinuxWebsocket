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

        // http header
        char header_buf[BUFFER_SIZE];
        memset(&header_buf, '\0', BUFFER_SIZE);
        int len = 0; // header len

        // 用于存放传输的文件
        char* file_buf;
        
        // 获取目标传输文件的属性, 比如说大小
        struct stat file_stat;
        if(stat(filename, &file_stat)<0){
            valid = false; // 文件不存在
        }
        else
        {
            if( S_ISDIR(file_stat.st_mode))
            {
                printf("ERROR: filename is a dir.\n");
                valid = false; //目标文件是个目录
            }
            else if(file_stat.st_mode & S_IROTH) // 用户有读取目标文件的权限
            {
                int fd = open(filename, O_RDONLY); //打开文件
                file_buf = new char[file_stat.st_size+1];
                memset(file_buf, '\0', file_stat.st_size+1);
                // 将文件内容读取到buffer
                if(read(fd,file_buf,file_stat.st_size)<0)
                {
                    printf("ERROR: read file error.\n");
                    valid = false;
                }
            }
            else
            {
                valid = false;
                printf("ERROR: can not access file.\n");
            }

            if(valid) // 传输的文件合法
            {
                // 把http头部信息写入header_buf
                ret = snprintf( header_buf, BUFFER_SIZE-1, "%s %s\r\n", "HTTP/1.1", status_line[0] );
                len += ret;
                ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, "Content-Length: %ld\r\n", file_stat.st_size );
                len += ret;
                ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, "%s", "\r\n" );

                struct iovec iv[2]; // 离散内存块, 用于装配http header 和 content;
                // http header
                iv[0].iov_base = header_buf;
                iv[0].iov_len = strlen(header_buf);// 错误写法: ret;
                // http content
                iv[1].iov_base = file_buf;
                iv[1].iov_len = file_stat.st_size;
                ret = writev(connfd,iv,2); 
            }
            else
            {   
                printf("500: file error.\n");
                ret = snprintf( header_buf, BUFFER_SIZE-1, "%s %s\r\n", "HTTP/1.1", status_line[1] );
                len += ret;
                ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, "%s", "\r\n" );
                send(connfd, header_buf, strlen(header_buf), 0);
            }
        }

        close(connfd);
        delete []file_buf;
    }

    close(sock);
    return 0;
}
