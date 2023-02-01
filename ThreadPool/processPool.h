//
// Created by 24508 on 2023-02-01.
//

#ifndef CLIONTEST_PROCESSPOOL_H
#define CLIONTEST_PROCESSPOOL_H

#include <sys/types.h>
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
#include <sys/stat.h>

// 用于处理信号的管道, 以实现统一事件源
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * 监听fd的 EPOLLIN | EPOLLET 事件
 * @param epollfd epoll 文件描述符
 * @param fd 监听目标的文件描述符
 */
static void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/**
 * @brief  取消监听某个fd
 * @note
 * @param  epollfd:
 * @param  fd:
 * @retval None
 */
static void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/**
 * @brief  接收到信号后, 把信号从1端传输到sig_pipe的0端
 * @note
 * @param  sig:
 * @retval None
 */
static void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

/**
 * @brief  绑定信号sig和其处理函数handler
 * @note
 * @param  sig: 信号
 * @param  handler: 处理函数
 * @retval None
 */
static void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

/*描述一个子进程的类, */
class process
{
public:
    process() : m_pid(-1) {}

public:
    pid_t m_pid;     // 子进程的pid
    int m_pipefd[2]; // 子进程和父进程通信的管道
};

/** **********************************************************************************************************************************************************************************************
 */
/*进程池类*/
template <typename T>
class processpool
{
private:
    // 初始化变量
    processpool(int listenfd, int process_number = 8);

public:
    /**
     * @brief  创建一个(仅)processpool实例
     * @note
     * @param  listenfd: 接受信息的socket fd
     * @param  process_number: 进程池的进程数量
     * @retval 一个静态的进程池实例
     */
    static processpool<T> *create(int listenfd, int process_number = 8)
    {
        if (!m_instance)
        {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete[] this->m_sub_process;
    }
    /**
     * @brief  启动进程池
     * @note
     * @retval None
     */
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    // 进程池允许的最大子进程数量
    static const int MAX_PROCESS_NUMBER = 16;
    // 每个子进程最多能处理的客户数量
    static const int USER_PER_PROCESS = 65536;
    // epoll 最多能处理的事件数
    static const int MAX_EVENT_NUMBER = 10000;
    // 进程池中的进程总数
    int m_process_number;
    // 子进程在进程池中的序号 (0 开始)
    int m_idx;
    // 子进程得到内核事件表
    int m_epollfd;
    // 监听 socket
    int m_listenfd;
    // 子进程通过m_stop来决定是否停止
    int m_stop;
    // 所有子进程的信息 process数组
    process *m_sub_process;
    // 进程池的静态实例  ??????????? 有什么用?
    static processpool<T> *m_instance = NULL;
};

template<typename T>
processpool<T>::processpool(int listenfd, int process_number)
    : m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false)
{
    assert( (process_number>0) && (process_number<this->MAX_PROCESS_NUMBER));
    m_sub_process = new process[process_number];
    assert(m_sub_process);

    // 创建process_number个子进程, 建立与父进程的管道联系
    for (int i = 0; i < process_number; ++i) {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(ret==0);
        // 创建子进程
        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid>=0);
        // 分别关闭父进程和子进程的管道
        if(m_sub_process[i].m_pid>0)
        {
            close(m_sub_process[i].m_pipefd[1]);
            continue; // 父进程继续创建子进程
        }
        else
        {
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break; // 子进程在这里不能再循环, 不然子进程会去创建子进程
        }
    }
}

/**
 * 统一事件源
 * @tparam T
 */
template<typename T>
void processpool<T>::setup_sig_pipe()
{
    // 创建epoll监听信号管道
    m_epollfd = epoll_create(5);
    assert(m_epollfd!=-1);
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret!=-1);
    setnonblocking(sig_pipefd[1]);
    addfd(m_epollfd, sig_pipefd[0]);

    // 设置信号处理函数
    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}


template<typename T>
void processpool<T>::run()
{
    // 父进程的m_idx为-1, 子进程的>=0
    if(m_idx==-1){
        run_parent();
    }
    else
    {
        run_child();
    }
}

/**
 * 子进程负责:
 * @tparam T (client data?)
 */
template<typename T>
void processpool<T>::run_child() // pipefd 父0子1
{
    // 每个进程(父/子)都会有一个管道来获取监听的信号
    setup_sig_pipe();
    int pipefd = m_sub_process[m_idx].m_pipefd[1]; //用于和父沟通的管道
    addfd(m_epollfd, pipefd);
    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS]; // ????????????????????????????
    assert(users);
    int number = 0;
    int ret = -1;
    while (!m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if( (number<0) && (errno!=EINTR))
        {
            printf("epoll failed\n");
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;
            // 收到了父从管道发来的client_id, 表示有新客户连接
            if((sockfd==pipefd) && (events[i].events & EPOLLIN))
            {
                int client = 0; // ???????????????????????????????????????????? 这是什么东西?
                ret = recv(sockfd, (char*)&client, sizeof(client), 0);
                if(((ret<0) && errno!= EAGAIN) || ret==0)
                {
                    continue;
                }
                else
                {
                    sockaddr_in client_address;
                    socklen_t client_address_len = sizeof(client_address);
                    int connfd = accept(m_listenfd, (sockaddr*)&client_address, &client_address_len);
                    if(connfd<0)
                    {
                        perror("accept");
                        continue;
                    }
                    addfd(m_epollfd, connfd);
                    // T的方法: 初始化一个客户端连接 ???????????????????????????????????????????
                    users[connfd].init(m_epollfd, connfd, client_address);
                }
            }
            // 收到了信号
            else if((sockfd==sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(sockfd, signals, sizeof(signals), 0);
                if(ret<=0)
                {
                    continue;
                }
                else
                {
                    for (int j = 0; j < ret; ++j) {
                        // 逐个处理信号
                        switch (signals[j]) {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG))>0)
                                {
                                    continue;
                                }
                                printf("child break because of signal\n");
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop = true;
                                printf("child break because of signal\n");
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            // 收到客户端请求了 (是什么请求呢)??????????????????????????
            else if(events[i].events & EPOLLIN)
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }
    delete[] users;
    users = NULL;
    close(pipefd);
    // close(m_listenfd); 不能这么做??????????????????? 为什么? 不是要引用-1吗?
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent()
{
    // 信号管道
    setup_sig_pipe();
    // 父进程监听m_listenfd
    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;
    while (!m_stop)
    {
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {
            int sockfd = events[i].data.fd;
            if(sockfd==m_listenfd)
            {
                // 采用round robin 选一个进程
                int j = sub_process_counter;
                do {
                    if(m_sub_process[j].m_pid!=-1)
                    {
                        break;
                    }
                    j = (j+1)%m_process_number;
                } while (j!=sub_process_counter);

                if(m_sub_process[j].m_pid==-1) // 父进程的m_sub_process.m_pid一定>0
                {
                    m_stop = true;
                    break;
                }

                sub_process_counter = (j+1)%m_process_number;
                // ??????????????????????????????????????????????????????????????????????? 为什么set new_conn? 它是个不变的值
                send(m_sub_process[i].m_pipefd[0], (char*)&new_conn, sizeof(new_conn), 0);
                printf("sent request to child %d\n",i);
            }
            // 处理信号
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for (int j = 0; j < ret; ++j) {
                        switch( signals[j] )
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    for( int k = 0; k < m_process_number; ++k )
                                    {
                                        if( m_sub_process[k].m_pid == pid )
                                        {
                                            printf( "child %d join\n", k );
                                            close( m_sub_process[k].m_pipefd[0] );
                                            m_sub_process[k].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
                                // 还有未结束的, 就先别停止
                                for (int k = 0; k < m_process_number; ++k) {
                                    if( m_sub_process[k].m_pid != -1 )
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                printf( "kill all the clild now\n" );
                                for( int k = 0; k < m_process_number; ++k )
                                {
                                    int pid = m_sub_process[k].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                                break;
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }
    close( m_listenfd );
     close( m_epollfd );
}
#endif //CLIONTEST_PROCESSPOOL_H
