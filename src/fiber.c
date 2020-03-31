#include <stdlib.h>
#include <unistd.h>

#include "fiber.h"

#define TASK_LIMIT (1024 * 128)
#define THREAD_LIMIT 256

static void *schedule_run(void *arg);

/* TODO: invoke clone() system call to create a light weight process.
 * sample usage:
 *   *tid = clone((int (*)(void*)) start_func, (char*) stack + _THREAD_STACK,
 *                SIGCHLD | CLONE_SIGHAND | CLONE_VM | CLONE_PTRACE, arg);
 */
static inline void open_pool(pool_t *pl)
{
    for (size_t i = 0; i < pl->size; ++i)
        pthread_create(&pl->threads[i]->tid, NULL, schedule_run,
                       (void *) pl->threads[i]);
}

static void close_pool(pool_t *pl)
{
    pl->stop = true;

    for (size_t i = 0; i < pl->size; ++i)
        pl->threads[i]->stop = true;

    for (size_t i = 0; i < pl->size; ++i) {
        int sval;
        schedule_t *sch = pl->threads[i];
        sem_getvalue(&sch->sem_free, &sval);
        if (sval != (int) sch->size)
            sem_wait(&sch->sem_done);
    }

    for (size_t i = 0; i < pl->size; ++i) {
        pthread_cancel(pl->threads[i]->tid);
        pthread_join(pl->threads[i]->tid, NULL);
    }
}

pool_t *create_pool(int thread_nums)
{
    if (thread_nums <= 0 || thread_nums > THREAD_LIMIT)
        return NULL;

    int tnums = 1;
    while (thread_nums > tnums)
        tnums *= 2;
    pool_t *pl = calloc(1, sizeof(pool_t));
    pl->size = tnums;
    pl->threads = calloc(tnums, sizeof(schedule_t *));
    pl->blocked_io_set = calloc(TASK_LIMIT, sizeof(task_t *));

    uint32_t qsize = TASK_LIMIT / tnums;
    for (int i = 0; i < tnums; ++i) {
        schedule_t *sch = calloc(1, sizeof(schedule_t));
        sch->size = qsize;
        sch->tasks = calloc(qsize, sizeof(task_t *));
        sem_init(&sch->sem_used, 0, 0);
        sem_init(&sch->sem_free, 0, qsize);
        pl->threads[i] = sch;
        sch->mpool = pl;
    }
    open_pool(pl);
    return pl;
}

void free_pool(pool_t *pool)
{
    close_pool(pool);
    for (size_t i = 0; i < pool->size; ++i) {
        schedule_t *sch = pool->threads[i];
        sem_destroy(&sch->sem_free);
        sem_destroy(&sch->sem_used);
        free(sch->tasks);
        free(sch);
    }
    free(pool->threads);
    free(pool->blocked_io_set);
    free(pool);
}

static void *schedule_run(void *arg)
{
    schedule_t *sch = (schedule_t *) arg;

    while (1) {
        sem_wait(&sch->sem_used);
        /* FIXME: remove unexpected usleep */
        while (!sch->tasks[sch->tail])
            usleep(1);
        while (sch->tasks[sch->tail]) {
            task_t *task = sch->tasks[sch->tail];
            switch (task->status) {
            case READY:
                getcontext(&task->ctx);
                task->ctx.uc_stack.ss_sp = task->stack;
                task->ctx.uc_stack.ss_size = STACK_SIZE;
                makecontext(&task->ctx, (void (*)(void)) task->entry, 1, task);
            case RUNABLE:
                task->status = RUNNING;
                sch->running = task;
                swapcontext(&sch->mctx, &task->ctx);
                break;
            default:
                break;
            }
            if (task->status == DEAD)
                free(task);
        }
        sch->tail = (sch->tail + 1) & (sch->size - 1);
    }
    return NULL;
}

int remove_task(task_t *task)
{
    if (!task)
        return -1;

    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;
    sem_post(&sch->sem_free);
    return 0;
}

static void task_entry(task_t *task)
{
    task->func(task, task->arg);
    task->status = DEAD;
    remove_task(task);
    schedule_t *sch = task->sch;
    if (sch->stop) {
        int sval;
        sem_getvalue(&sch->sem_free, &sval);
        if (sval == (int) sch->size)
            sem_post(&sch->sem_done);
    }
    swapcontext(&task->ctx, &sch->mctx);
}

task_t *create_task(void (*func)(task_t *, void *), void *arg)
{
    task_t *ptask = calloc(1, sizeof(task_t));
    ptask->entry = task_entry;
    ptask->func = func;
    ptask->arg = arg;
    ptask->status = READY;
    return ptask;
}

int add_task(pool_t *pool, task_t *task)
{
    if (!pool || !task || pool->stop)
        return -1;

    schedule_t *sch = pool->threads[pool->index];
    while (sem_trywait(&sch->sem_free) != 0) {
        pool->index = (pool->index + 1) & (pool->size - 1);
        sch = pool->threads[pool->index];
    }

    task->sch = sch;
    int index = (int) (__sync_fetch_and_add(&sch->head, 1) & (sch->size - 1));
    sch->tasks[index] = task;
    sem_post(&sch->sem_used);
    pool->index = (pool->index + 1) & (pool->size - 1);
    return 0;
}

int yield_task(task_t *task)
{
    if (!task)
        return -1;

    task->status = RUNABLE;
    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;
    int index = (int) (__sync_fetch_and_add(&sch->head, 1) & (sch->size - 1));
    sch->tasks[index] = task;
    sem_post(&sch->sem_used);
    swapcontext(&task->ctx, &sch->mctx);
    return 0;
}

int suspend_task(task_t *task)
{
    if (!task)
        return -1;

    task->status = SUSPEND;
    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;
    return 0;
}

int suspend_fd(task_t *task, int fd)
{
    if (!task || fd < 0)
        return -1;

    suspend_task(task);
    task->sch->mpool->blocked_io_set[fd] = task;
    return 0;
}

int wake_task(task_t *task)
{
    if (!task)
        return -1;

    task->status = RUNABLE;
    schedule_t *sch = task->sch;
    int index = (int) (__sync_fetch_and_add(&sch->head, 1) & (sch->size - 1));
    sch->tasks[index] = task;
    sem_post(&sch->sem_used);
    return 0;
}

int wake_fd(pool_t *pool, int fd)
{
    if (!pool || fd < 0)
        return -1;

    return wake_task(pool->blocked_io_set[fd]);
}
