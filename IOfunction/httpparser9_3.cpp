#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096

/*主状态机的状态, parsing request line or http headers*/
enum CHECK_STATE {CHECK_STATE_REQUESTLINE=0, CHECK_STATE_HEADER};
/*从状态机的状态, 行完整, 行出错, 行数据不完整*/
enum LINE_STATUS {LINE_OK = 0, LINE_BAD, LINE_OPEN};
/*Http请求结果 请求不完整,    完整,   请求有语法错误, 客户端没有访问资源的权利, 服务器内部错误, 客户端已关闭*/
enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};

/*Http 应答 (简化)*/
static const char* szret[] = { "I get a correct result\n", "Something wrong\n" };

/**
 * checked_index: 正在分析的字节
 * read_index: 客户端数据尾部的下一字节
*/
LINE_STATUS parse_line( char* buffer, int& checked_index, int& read_index )
{
    char temp;
    for(; checked_index<read_index; ++checked_index)
    {
        temp = buffer[checked_index]; // 当前分析的字节
        if(temp=='\r'){
            if((checked_index+1)==read_index) return LINE_OPEN;
            else if(buffer[checked_index+1]=='\n')
            {
                buffer[checked_index++] = '\0';  // ???????????????
                buffer[checked_index++] = '\0';
                return LINE_OK; //最终指向\n的下一个
            }
            return LINE_BAD; // 有'\r'后面没有'\n', 语法错误
        }
        else if(temp=='\n')   // 这个判断可以删除!?
        {
            if((checked_index>1) && buffer[checked_index-1]=='\r')
            {
                buffer[checked_index-1] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK; //最终指向\n的下一个
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; //根本没遇到\r, 说明还得继续读取.
}

/**
 * 分析请求行
 * temp:
 * checkstate: 
*/

HTTP_CODE parse_requestline(char* temp, CHECK_STATE& checkstate )
{
    char* url = strpbrk(temp, " \t");
    if(!url)
    {
        // http请求行没有空格或者\t, 那么请求就是出问题了
        return BAD_REQUEST;
    }
    *url = '\0'; //空格或者\t换成这个
    url++; // 移动指针
    
    char* method = temp;
    if ( strcasecmp( method, "GET" ) == 0 ) 
    {
        printf( "The request method is GET\n" );
    }
    else
    {
        return BAD_REQUEST;
    }

    url += strspn( url, " \t" ); // 把空格读完, url指向第一个不是空格的位置
    char* version = strpbrk( url, " \t" ); // 找到下一个空格, 该空格后面是http版本
    if ( ! version )
    {
        return BAD_REQUEST;
    }
    *version++ = '\0'; // 此时version指向http 版本char*第一个
    version += strspn( version, " \t" ); // 空格读完

    /*仅支持http/1.1*/
    if(strcasecmp(version, "HTTP/1.1")!=0)
    {
        return BAD_REQUEST;
    }

    /*检测url是否合法*/
    if(strncasecmp(url, "http://", 7)==0)
    {
        url+=7;
        url = strchr(url, '/'); //?????????不知道在干嘛, 这时候不是指向了//的后面吗?
    }
    if ( ! url || url[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }
    printf( "The request URL is: %s\n", url );
    checkstate = CHECK_STATE_HEADER;
    return NO_REQUEST;


}

int main(int argc, char const *argv[])
{
    char* temp;
    temp = (char*) malloc(32);
    memset(temp,'\0', 32);
    strcpy(temp, "GET http://www.w3.html HTTP/1.1")
    return 0;
}


