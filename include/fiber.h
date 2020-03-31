#ifndef __FIBER_H
#define __FIBER_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <ucontext.h>

#define STACK_SIZE (4096 * 32)

enum TASK_STATUS {
    READY = 0,
    RUNABLE = 1,
    RUNNING = 2,
    SUSPEND = 3,
    DEAD = 4,
};

typedef struct task {
    void (*entry)(struct task *);

    void (*func)(struct task *, void *);

    void *arg;
    int status;
    char stack[STACK_SIZE];
    ucontext_t ctx;
    struct schedule *sch;
} task_t;

typedef struct schedule {
    bool stop;
    sem_t sem_done;
    sem_t sem_free;
    sem_t sem_used;
    volatile size_t head;
    size_t tail;
    size_t size;
    task_t **tasks;
    task_t *running;
    struct pool *mpool;
    pthread_t tid;
    ucontext_t mctx;
} schedule_t;

typedef struct pool {
    bool stop;
    size_t size;
    size_t index;
    int epoll_fd;
    pthread_t main_tid;
    schedule_t **threads;
    task_t **blocked_io_set;
} pool_t;

pool_t *create_pool(int thread_num);

void free_pool(pool_t *pl);

task_t *create_task(void (*func)(task_t *, void *), void *arg);

int add_task(pool_t *pl, task_t *tsk);

int suspend_task(task_t *tsk);

int yield_task(task_t *tsk);

int wake_task(task_t *task);

int suspend_fd(task_t *task, int fd);

int wake_fd(pool_t *pl, int fd);

#endif
