#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <wait.h>

pthread_mutex_t mutex;

void *another(void *arg)
{
    while (1)
    {
        pthread_mutex_lock(&mutex);
        printf("in child thread, lock the mutex\n");
        sleep(1);
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

void prepare()
{
    pthread_mutex_lock(&mutex);
}

void infork()
{
    pthread_mutex_unlock(&mutex);
}

int main()
{
    pthread_mutex_init(&mutex, NULL);
    pthread_t id;
    pthread_create(&id, NULL, another, NULL);
    sleep(1);
    pthread_atfork(prepare, infork, infork); // 在fork前调用这个, 子进程就不会发现锁已经被lock了
    int pid = fork();
    if (pid < 0)
    {
        pthread_join(id, NULL);
        pthread_mutex_destroy(&mutex);
        return 1;
    }
    else if (pid == 0)
    {
        // 子进程也尝试获取锁, 但是会发现锁变量已经被上锁了
        printf("I anm in the child, want to get the lock\n");
        pthread_mutex_lock(&mutex);
        printf("I can not run to here, oop...\n");
        pthread_mutex_unlock(&mutex);
        exit(0);
    }
    else
    {
        wait(NULL); // 等待子进程死亡
    }
    pthread_join(id, NULL);
    pthread_mutex_destroy(&mutex);
    return 0;
}