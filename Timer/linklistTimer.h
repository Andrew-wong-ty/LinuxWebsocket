#ifndef _LINKLIST_TIMER_
#define _LINKLIST_TIMER_
#include <stdio.h>
#include <time.h>
#include <netinet/in.h>
#define BUFFER_SIZE 64

class util_timer;

struct client_data
{
    sockaddr_in address;
    int socket_fd;
    char buf[BUFFER_SIZE];
    util_timer* timer;
};

class util_timer
{
public:
    util_timer(): prev(NULL), next(NULL){}

public:
    time_t expire; 
    void (*cb_func)( client_data* ); // 定时器的回调函数??
    client_data* user_data;
    util_timer* prev;
    util_timer* next;
};

class sort_timer_lst
{
public:
    sort_timer_lst(): head(NULL), tail(NULL) {}
    ~sort_timer_lst()
    {
        util_timer* temp = head;
        while (temp)
        {
            head = temp->next;
            delete temp;
            temp = head;
        }
    }

    // 添加一个定时器到链表中
    void add_timer(util_timer* timer)
    {
        if(!timer) return;
        if(!head)
        {
            head = tail = timer;
            return;
        }
        if(timer->expire<head->expire)
        {
            // 添加到头
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        else 
        {
            add_timer(timer, head->next);
        }
    }
    
    // 调整超时延长的timer
    void adjust_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        util_timer* tmp = timer->next;
        if( !tmp || ( timer->expire < tmp->expire ) )
        {
            return;
        }
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer( timer, head );
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer( timer, timer->next );
        }

    }
    
    // 删除指针指向的timer
    void del_timer( util_timer* timer )
    {
        if( !timer )
        {
            return;
        }
        if( ( timer == head ) && ( timer == tail ) )
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if( timer == head )
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if( timer == tail )
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    // SIGALRM信号触发时执行
    void tick()
    {
        if(!head)
        {
            return;
        }
        printf( "timer tick\n" );
        time_t cur = time( NULL ); // 系统当前时间
        util_timer* tmp = head;
        while( tmp )
        {
            if( cur < tmp->expire )// 小于head, 说明其它的所有事件都还没expire
            {
                break;
            }
            // 执行定时任务
            tmp->cb_func(tmp->user_data);
            // 执行完就删除 (都是从head开始执行的)
            head = tmp->next;
            if(head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;

        }

    }

private:
    /**
     * 把timer加入到合适的位置, where timer.expier>=prev but <=next
    */
    void add_timer(util_timer* timer, util_timer* lst_head )
    {
        util_timer* prev = lst_head ;
        util_timer* tmp = prev->next;
        while( tmp )
        {
            if( timer->expire < tmp->expire )
            {
                timer->next = tmp;
                timer->prev = prev;
                prev->next = timer;
                tmp->prev = timer;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if(!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            this->tail = timer;
        }
    }

private:
    util_timer* head;
    util_timer* tail;
};


#endif