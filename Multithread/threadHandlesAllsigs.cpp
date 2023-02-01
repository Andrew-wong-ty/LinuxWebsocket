#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

/**
 * @brief  信号处理函数
 * @note
 * @retval
 */
#define handle_error_en(en, msg) \
    do                           \
    {                            \
        errno = en;              \
        perror(msg);             \
        exit(EXIT_FAILURE);      \
    } while (0)

static void *sig_thread(void *arg)
{
    sigset_t *set = (sigset_t *)arg;
    int s, sig;
    for (;;)
    {
        s = sigwait(set, &sig);
        if (s != 0)
        {
            handle_error_en(s, "sigwait");
        }
        printf("Signal handling thread got signal %d\n", sig);
    }
}

int main(int argc, char const *argv[])
{
    pthread_t thread;
    sigset_t set;
    int s;

    /*设置信号掩码*/
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGUSR1); 
    s = pthread_sigmask(SIG_BLOCK, &set, NULL ); // 设置信号掩码 //
    if(s!=0) handle_error_en(s, "pthread_sigmask");
    s = pthread_create(&thread, NULL, &sig_thread, &set);
    if(s!=0) handle_error_en(s, "pthread_create");
    pthread_kill(thread, SIGUSR2); // 主动发送某个信号给线程
    sleep(10);
    return 0;
}
