#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * 描述:
 * 创建一个多进程聊天服务器
 *
 */

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535 // epoll fd最大数量
#define MAX_EVENT_NUMBER 1024
#define PROCESS_LIMIT 4194304

struct client_data
{
    sockaddr_in address;
    int connfd; // 子进程负责的client
    pid_t pid; /*子进程pid*/
    int pipefd[2];//用于和父进程沟通的管道, 用于子进程在成功读取客户数据到缓存后, 发送客户id给主进程
    
};

static const char *shm_name = "/my_shm";
int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char *share_mem = 0;

/*客户连接数组, 客户编号->client_data*/
client_data *users = 0;
/*子进程和客户连接的映射关系表, PID->PID处理的客户编号*/
int *sub_process = 0;
/*当前客户数量*/
int user_count = 0;
bool stop_child = false;


int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


/**
 * @brief  添加fd的EPOLLIN就绪事件
 * @note   监听EPOLLIN | EPOLLET
 * @param  epollfd: epoll 文件描述符
 * @param  fd: 需要监听的文件描述符
 * @retval None
 */
void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 传输信号到sig_pipe[0]
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void addsig(int sig, void (*handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART; // 自动重启动被信号中断的系统调用
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void del_resource()
{
    close(epollfd);
    close(listenfd);
    close(sig_pipefd[0]);
    close(sig_pipefd[1]);
    shm_unlink(shm_name);
    delete[] users;
    delete[] sub_process;
}

/*停止一个子进程*/
void child_term_handler(int sig)
{
    stop_child = true;
}

/**
 * 子进程运行的函数
 * idx: 子进程正在处理的客户在users中的索引
 * users: 所有的客户
 * shm_men: 共享内存的起始地址
 */
int run_child(int idx, client_data *users, char *shm_men)
{
    epoll_event events[MAX_EVENT_NUMBER];
    // 子进程使用IO复用技术来监听客户的IO信号, 以及与父进程通信的管道文件fd
    int child_epollfd = epoll_create(5);
    assert(child_epollfd != -1);
    int connfd = users[idx].connfd; // 此子进程负责的客户
    addfd(child_epollfd, connfd);
    int pipefd = users[idx].pipefd[1]; //  子进程在1端写客户idx给父进程0 or 父进程从0写给子进程1
    addfd(child_epollfd, pipefd);
    int ret;

    /*子进程设置自己的信号处理函数*/
    addsig(SIGTERM, child_term_handler, false);

    while (!stop_child)
    {
        int number = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll fail, code:%d\n", errno);
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < number; i++)
        {
            
            int sockfd = events[i].data.fd;
            // 子进程负责的客户连接有数据到达, 发送idx给主进程
            if ((sockfd == connfd) && (events[i].events & EPOLLIN))
            {
                
                memset(shm_men + idx * BUFFER_SIZE, '\0', BUFFER_SIZE);
                /*把客户数据读取到缓存中*/
                int ret = recv(sockfd, shm_men + idx * BUFFER_SIZE, BUFFER_SIZE - 1, 0);
                if (ret < 0)
                {
                    if (errno != EAGAIN) // EAGAIN: 非阻塞下, 发送缓存被占满
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0)
                {
                    stop_child = true;
                }
                else
                {
                    // 用管道发送客户id过去, 让主进程处理
                    send(pipefd, (char *)&idx, sizeof(idx), 0);
                }
            }
            // 广播: 父进程把client的索引idx发到负责的子进程 pipefd为1端
            // - 有别的客户端OT发消息的话, 父进程会把该客户端的idx发给所有子进程,
            // - 子进程通过idx在共享内存读取OT的数据, 然后发给自己负责的客户
            else if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
            {
                int client_idx = -1;
                ret = recv(sockfd, (char *)&client_idx, sizeof(client_idx), 0); // 接收该index
                if (ret < 0)
                {
                    if (errno != EAGAIN) // EAGAIN: 非阻塞下, 发送缓存被占满
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0)
                {
                    stop_child = true;
                }
                else
                {
                    // 把其它客户端的消息发给自己负责的客户端
                    
                    send(connfd, shm_men + client_idx * BUFFER_SIZE, BUFFER_SIZE, 0);
                    
                }
            }
            else
                continue;
        }
    }
    close(child_epollfd);
    close(connfd);
    close(pipefd);
    return 0;
}

int main(int argc, char const *argv[])
{
    const char* ip = "10.0.4.7"; 
    // int port = 12345;
    int port = atoi( argv[1] );
    printf("124.223.47.124 %d\n", port);
    printf("ip:%s, port:%d\n",ip,port);

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    // 初始化用户数量, idx->client_data表, pid->idx表
    user_count = 0;
    users = new client_data [ USER_LIMIT+1 ];
    sub_process = new int [ PROCESS_LIMIT ];
    for( int i = 0; i < PROCESS_LIMIT; ++i )
    {
        sub_process[i] = -1;
    }

    // (IO复用)监听listenfd 读就绪
    epoll_event events[ MAX_EVENT_NUMBER ];
    epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd );

    // 创建信号传输管道, 并监听
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    assert( ret != -1 );
    setnonblocking( sig_pipefd[1] );
    addfd( epollfd, sig_pipefd[0] );

    // 绑定信号与handler
    addsig( SIGCHLD, sig_handler );
    addsig( SIGTERM, sig_handler );
    addsig( SIGINT, sig_handler );
    addsig( SIGPIPE, SIG_IGN );

    bool stop_server = false;
    bool terminate = false;

    // 创建共享内存
    shmfd = shm_open( shm_name, O_CREAT | O_RDWR, 0666 );
    assert( shmfd != -1 );
    ret = ftruncate( shmfd, USER_LIMIT * BUFFER_SIZE );  // 改变shmfd的大小
    assert( ret != -1 );

    share_mem = (char*) mmap(NULL, USER_LIMIT * BUFFER_SIZE, 
    PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    assert( share_mem != MAP_FAILED );
    close(shmfd); // 为什么立马关闭?

    while (!stop_server)
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }
        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            // 有客户端发起请求, accept并且fork一个子进程来处理
            if( sockfd == listenfd ) 
            {
                
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( user_count >= USER_LIMIT )
                {
                    const char* info = "too many users\n";
                    printf( "%s", info );
                    send( connfd, info, strlen( info ), 0 );
                    close( connfd );
                    continue;
                }
                users[user_count].address = client_address;
                users[user_count].connfd = connfd;
                ret = socketpair( PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd ); // 初始化父子进程通信的fd
                assert( ret != -1 );
                pid_t pid = fork();
                if( pid < 0 )
                {
                    close( connfd );
                    continue;
                }
                else if( pid == 0 )
                {
                    // 子进程
                    close( epollfd );  // 子进程会创建自己的IO复用
                    close( listenfd ); // 子进程不需要处理server fd (这是父进程负责的)
                    close( users[user_count].pipefd[0] ); // 子进程只会在1端写入, 因此关闭0端
                    // 子进程不需要处理信号
                    close( sig_pipefd[0] );
                    close( sig_pipefd[1] );
                    // 执行子进程的函数
                    run_child( user_count, users, share_mem );
                    
                    munmap( (void*)share_mem,  USER_LIMIT * BUFFER_SIZE ); // 子进程分离共享内存 (但还没释放, 要shm_unlink)
                    
                    exit( 0 );
                }
                else
                {
                    
                    //父进程
                    close(connfd); // 父进程不负责接受connfd的数据, 是子进程干的
                    close(users[user_count].pipefd[1]); // 子进程从1写, 父进程从0接收, 因此1对于父进程没有, 关掉
                    // 父进程监听pipefd 0, 因为子进程会从1发client_id到0
                    addfd(epollfd, users[user_count].pipefd[0]);
                    users[user_count].pid = pid; // 设置子进程pid
                    printf("pid: %d\n", pid);
                    sub_process[pid] = user_count; // pid->user idx
                    user_count++;
                    
                }


            }
            // 处理信号
            else if((sockfd==sig_pipefd[0])&&(events[i].events && EPOLLIN))
            {
                int sig;
                char signals[1024]; // 多个进程可能有多个信号等待处理, 因此一次性处理多个信号
                ret = recv(sockfd, signals, sizeof(signals), 0);
                if(ret<=0) continue;
                else
                {
                    for (int i = 0; i < ret; i++)
                    {
                        switch (signals[i])
                        {
                            case SIGCHLD: // 子进程退出, 表示有客户端关闭了连接
                            {
                                pid_t pid;
                                int stat;
                                while ( (pid = waitpid(-1, &stat, WNOHANG )) >0 ) // -1表示match 任意死掉的子进程
                                {
                                    int dead_client_idx = sub_process[pid]; // 获取用户idx  -------------------------------------
                                    sub_process[pid] = -1;
                                    if( (dead_client_idx<0) || (dead_client_idx>USER_LIMIT) )
                                    {
                                        continue; // 这些情况下, 还没有分配client_epoll等, 因此直接跳过即可
                                    }
                                    // 清除dead_client_idx 相关数据
                                    // 删除已注册的event事件, 不再监听请求了
                                    epoll_ctl(epollfd, EPOLL_CTL_DEL, users[dead_client_idx].pipefd[0], 0);
                                    close(users[dead_client_idx].pipefd[0]);
                                    // 一直都有的疑问: 为什么这么处理?
                                    users[dead_client_idx] = users[--user_count];// 把(usercount-1)的client换到死client
                                    sub_process[users[dead_client_idx].pid] = dead_client_idx; //更新死pid->client_idx
                                    printf( "child %d exit, now we have %d users\n", dead_client_idx, user_count ); 
                                }
                                if(terminate && user_count ==0 )
                                {
                                    stop_server = true;
                                }
                                break; //退出switch
                            }
                            case SIGTERM:
                            case SIGINT: // CTRL+C
                            {
                                printf( "kill all the clild now\n" );
                                //addsig( SIGTERM, SIG_IGN );
                                //addsig( SIGINT, SIG_IGN );
                                if( user_count == 0 )
                                {
                                    stop_server = true;
                                    break;
                                }
                                for (int i = 0; i < user_count; i++)
                                {
                                    pid_t pid = users[i].pid;
                                    kill(pid, SIGTERM); // send signal to pid;
                                }
                                terminate = true;
                                break;
                                
                            }
                            default:
                                break;
                        }
                    }
                    
                }
            }
            else if(events[i].events && EPOLLIN) //子进程发送user idx给父进程, 请求广播消息
            {
                int client_idx_broadcast = 0;
                // 此时sockfd为某个client_idx的client的pipefd[0] (因为子进程从1发到0)
                ret = recv(sockfd, (char*)&client_idx_broadcast, sizeof(client_idx_broadcast), 0);
                printf( "read data from child accross pipe\n" );
                if( ret == -1 )
                {
                    continue;
                }
                else if( ret == 0 )
                {
                    continue;
                }
                else
                {
                    for( int j = 0; j < user_count; ++j )
                    {
                        //遍历所有客户, 发送该客户的消息
                        if(users[j].pipefd[0] == sockfd) continue;
                        else
                        {
                            // 向父进程发送需要广播的client_idx (0->1)
                            send(users[j].pipefd[0], (char*)&client_idx_broadcast,
                                sizeof(client_idx_broadcast), 0);
                        }
                    }
                }
            }
        }

    }
    


    del_resource();
    return 0;
}
