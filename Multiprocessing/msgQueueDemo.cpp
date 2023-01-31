#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/msg.h>
#include <string.h>
#define BUFFER_SIZE 256

/**
 * @brief  自定义的消息队列的消息结构,
 * @note    这个结构必须先是long再是char[]
 * * (https://man7.org/linux/man-pages/man3/msgrcv.3p.html)
 * @retval None
 */
struct mymsg
{
    long mtype; /* Message type. */
    char mtext[BUFFER_SIZE];
};

int main(int argc, char const *argv[])
{
    int queid = msgget(IPC_PRIVATE, IPC_CREAT | IPC_EXCL);
    pid_t pid = fork();
    if (pid == 0) // 子进程发消息到消息队列
    {
        // 定义用于发送的消息结构体
        mymsg child_buf;
        child_buf.mtype = 1;
        strcpy(child_buf.mtext, "hello world");
        printf("child sent msg: %s\n", child_buf.mtext);
        int ret = msgsnd(queid, &child_buf, strlen(child_buf.mtext), IPC_NOWAIT);
        assert(ret != -1);
    }
    else
    {
        // 父进程不断问询消息队列, 获取消息
        while (1)
        {
            mymsg msg_rcv; // 用于接收的消息结构体
            int ret = msgrcv(queid, &msg_rcv, BUFFER_SIZE, 0, IPC_NOWAIT);
            if (ret != -1)
            {
                printf("parent received msg: %s\n", msg_rcv.mtext);
                break;
            }
        }
    }
    return 0;
}
