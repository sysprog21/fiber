#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "fiber_cond.h"

#define COND_WAIT_QUEUE_SIZE (1024 * 64)
#define COND_WAIT_QUEUE_INDEX_MASK 0xffff
#define COND_WAIT_QUEUE_LEN_MASK 0xffffffff

int fiber_cond_init(fiber_cond_t *cond)
{
    if (!cond)
        return -1;

    cond->value = 0;
    cond->wait_queue = calloc(COND_WAIT_QUEUE_SIZE, sizeof(task_t *));
    return 0;
}

int fiber_cond_destroy(fiber_cond_t *cond)
{
    if (!cond || (cond->value & COND_WAIT_QUEUE_LEN_MASK) != 0)
        return -1;

    free(cond->wait_queue);
    cond->value = 0;
    cond->wait_queue = NULL;
    return 0;
}

int fiber_cond_wait(task_t *task, fiber_cond_t *cond, fiber_mutex_t *mtx)
{
    uint64_t value = __sync_fetch_and_add(&cond->value, 0x100000001);
    uint32_t len = value & COND_WAIT_QUEUE_LEN_MASK;

    assert(len < COND_WAIT_QUEUE_SIZE);

    /* suspend the task, and put it on the wait queue */
    task->status = SUSPEND;
    uint32_t index = (value >> 32) & COND_WAIT_QUEUE_INDEX_MASK;
    cond->wait_queue[index] = task;

    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;

    /* release the mutex */
    fiber_mutex_unlock(mtx);
    swapcontext(&task->ctx, &sch->mctx);

    /* after task be woke up, get the mutex first */
    fiber_mutex_lock(task, mtx);
    return 0;
}

int fiber_cond_signal(fiber_cond_t *cond)
{
    uint64_t value, new_value;
    do {
        value = cond->value;
        new_value =
            (value & 0xffffffff) ? (value - 1) : (value & 0xffffffff00000000);
    } while (!__sync_bool_compare_and_swap(&cond->value, value, new_value));

    uint32_t len = value & COND_WAIT_QUEUE_LEN_MASK;
    if (len == 0) {
        /* no task is on the wait queue, return immediately */
        return 0;
    }

    /* get the wait queue tail index, the task must be woke up was put on there
     */
    uint32_t index = ((value >> 32) - len) & COND_WAIT_QUEUE_INDEX_MASK;
    /* FIXME: do not depend on usleep */
    while (!cond->wait_queue[index])
        usleep(1);

    /* remove the task from the wait queue */
    task_t *waked_task = cond->wait_queue[index];
    cond->wait_queue[index] = NULL;
    waked_task->status = RUNABLE;

    schedule_t *sch = waked_task->sch;
    uint16_t pos = __sync_fetch_and_add(&sch->head, 1) & (sch->size - 1);
    sch->tasks[pos] = waked_task;
    sem_post(&sch->sem_used);
    return 0;
}

int fiber_cond_broadcast(fiber_cond_t *cond)
{
    uint64_t value = __sync_fetch_and_and(&cond->value, 0xffffffff00000000);
    uint32_t len = value & COND_WAIT_QUEUE_LEN_MASK;

    /* wake all tasks on the wait queue up */
    uint32_t first_index = ((value >> 32) - len) & COND_WAIT_QUEUE_INDEX_MASK;
    size_t i;
    for (i = 0; i < len; ++i) {
        uint32_t index = (first_index + i) & COND_WAIT_QUEUE_INDEX_MASK;
        /* FIXME: do not depend on usleep */
        while (!cond->wait_queue[index])
            usleep(1);

        /* remove the task from the wait queue */
        task_t *waked_task = cond->wait_queue[index];
        cond->wait_queue[index] = NULL;
        waked_task->status = RUNABLE;

        schedule_t *sch = waked_task->sch;
        uint16_t pos = __sync_fetch_and_add(&sch->head, 1) & (sch->size - 1);
        sch->tasks[pos] = waked_task;
        sem_post(&sch->sem_used);
    }
    return 0;
}
