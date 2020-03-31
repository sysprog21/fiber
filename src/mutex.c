#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "fiber_mutex.h"

#define MUTEX_WAIT_QUEUE_SIZE (1024 * 64)
#define MUTEX_WAIT_QUEUE_INDEX_MASK 0xffff
#define MUTEX_WAIT_QUEUE_LEN_MASK 0xffffffff

int fiber_mutex_init(fiber_mutex_t *mtx)
{
    if (!mtx)
        return -1;

    mtx->value = 0;
    mtx->wait_queue = calloc(MUTEX_WAIT_QUEUE_SIZE, sizeof(task_t *));
    return 0;
}

int fiber_mutex_destroy(fiber_mutex_t *mtx)
{
    if (!mtx || (mtx->value & MUTEX_WAIT_QUEUE_LEN_MASK))
        return -1;

    free(mtx->wait_queue);
    mtx->value = 0;
    mtx->wait_queue = NULL;
    return 0;
}

int fiber_mutex_lock(task_t *task, fiber_mutex_t *mtx)
{
    uint64_t value = __sync_fetch_and_add(&mtx->value, 0x100000001);
    uint32_t len = value & MUTEX_WAIT_QUEUE_LEN_MASK;

    assert(len < MUTEX_WAIT_QUEUE_SIZE);
    if (len == 0) {
        /* no task is holding the lock, got the lock */
        return 0;
    }

    /* suspend the task, and put it on the wait queue */
    task->status = SUSPEND;
    uint32_t index = (value >> 32) & MUTEX_WAIT_QUEUE_INDEX_MASK;
    mtx->wait_queue[index] = task;

    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;
    swapcontext(&task->ctx, &sch->mctx);
    return 0;
}

int fiber_mutex_unlock(fiber_mutex_t *mtx)
{
    uint64_t value = __sync_sub_and_fetch(&mtx->value, 0x1);
    uint32_t len = value & MUTEX_WAIT_QUEUE_LEN_MASK;
    if (len == 0) {
        /* no task is on the wait queue, return immediately */
        return 0;
    }

    /* get the wait queue tail index, the task must be woke up was put on there
     */
    uint32_t index = ((value >> 32) - len) & MUTEX_WAIT_QUEUE_INDEX_MASK;
    /* FIXME: do not depend on usleep */
    while (!mtx->wait_queue[index])
        usleep(1);

    /* remove the task from the wait queue */
    task_t *waked_task = mtx->wait_queue[index];
    mtx->wait_queue[index] = NULL;
    waked_task->status = RUNABLE;

    schedule_t *sch = waked_task->sch;
    uint16_t pos = __sync_fetch_and_add(&sch->head, 1) & (sch->size - 1);
    sch->tasks[pos] = waked_task;
    sem_post(&sch->sem_used);

    return 0;
}
