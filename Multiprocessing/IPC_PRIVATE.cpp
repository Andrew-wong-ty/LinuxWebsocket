#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

union semun
{
    int val;
    semid_ds* buf; //Data structure describing a set of semaphores.
    unsigned short int* array;
    seminfo* __buf;
};


void pv(int sem_id, int op)
{
    sembuf sem_b; // pv操作结构体
    sem_b.sem_num = 0;
    sem_b.sem_op = op;
    sem_b.sem_flg = SEM_UNDO; // 当进程退出时, 取消正在进行的OP操作
    semop(sem_id, &sem_b, 1); // 要执行的操作个数为1
}

int main(int argc, char const *argv[])
{
    int sem_id = semget(IPC_PRIVATE, 1, 0666); //创建一个初始个数为1,权限为0666的信号量集
    semun sem_un;
    sem_un.val = 1;
    semctl(sem_id, 0, SETVAL, sem_un);

    pid_t id = fork();
    if(id<0)
    {
        return 1;
    }
    else if(id==0)
    {
        printf("child \n");
        pv(sem_id, -1); // 获取信号量
        printf("child get sem and will release it after 5 seconds\n");
        sleep(2);
        pv(sem_id, 1);  // 信号量+1
        exit(0);
    }
    else
    {
        printf("parent \n");
        pv(sem_id, -1); // 获取信号量
        printf("parent get sem and will release it after 5 seconds\n");
        sleep(2);
        pv(sem_id, 1);  // 信号量+1
    }

    waitpid(id, NULL, 0);
    semctl(sem_id, 0, IPC_RMID, sem_un); // 删除信号量



    return 0;
}

