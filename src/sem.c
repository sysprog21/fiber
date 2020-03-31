#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "fiber_sem.h"

#define SEM_VALUE_MAX 0xffe0000
#define SEM_VALUE_MASK 0xffffffff
#define SEM_WAIT_QUEUE_SIZE (1024 * 64)
#define SEM_WAIT_QUEUE_INDEX_MASK 0xffff

int fiber_sem_init(fiber_sem_t *sem, int value)
{
    if (!sem || value < 0 || value >= SEM_VALUE_MAX)
        return -1;
    uint64_t tval = SEM_VALUE_MAX - value;
    sem->value = (tval << 32) + tval;
    sem->wait_queue = calloc(SEM_WAIT_QUEUE_SIZE, sizeof(task_t *));
    return 0;
}

int fiber_sem_destroy(fiber_sem_t *sem)
{
    if (!sem || (sem->value & SEM_VALUE_MASK) != SEM_VALUE_MAX)
        return -1;
    free(sem->wait_queue);
    sem->value = 0;
    sem->wait_queue = NULL;
    return 0;
}

int fiber_sem_wait(task_t *task, fiber_sem_t *sem)
{
    uint64_t value = __sync_fetch_and_add(&sem->value, 0x100000001);
    uint32_t val = value & SEM_VALUE_MASK;

    assert(val < SEM_VALUE_MAX + 0x10000);
    if (val < SEM_VALUE_MAX) {
        /* value of the semaphore is greater than zero, return */
        return 0;
    }

    /* suspend the task, and put it on the wait queue */
    task->status = SUSPEND;
    uint32_t index = (value >> 32) & SEM_WAIT_QUEUE_INDEX_MASK;
    sem->wait_queue[index] = task;

    schedule_t *sch = task->sch;
    sch->tasks[sch->tail] = NULL;
    swapcontext(&task->ctx, &sch->mctx);
    return 0;
}

int fiber_sem_post(fiber_sem_t *sem)
{
    uint64_t value = __sync_sub_and_fetch(&sem->value, 0x1);
    uint32_t val = value & SEM_VALUE_MASK;

    assert(val < SEM_VALUE_MAX + 0x10000);
    if (val < SEM_VALUE_MAX) {
        /* no task is on the wait queue, return immediately */
        return 0;
    }

    /* get the wait queue tail index, the task must be woke up was put on there
     */
    uint32_t index = ((value >> 32) - val - 1) & SEM_WAIT_QUEUE_INDEX_MASK;
    while (sem->wait_queue[index] == NULL) {
        usleep(1);
    }

    /* remove the task from the wait queue */
    task_t *waked_task = sem->wait_queue[index];
    sem->wait_queue[index] = NULL;
    waked_task->status = RUNABLE;

    schedule_t *sch = waked_task->sch;
    uint16_t pos = __sync_fetch_and_add(&sch->head, 1) & (sch->size - 1);
    sch->tasks[pos] = waked_task;
    sem_post(&sch->sem_used);
    return 0;
}

int fiber_sem_getvalue(fiber_sem_t *sem, int *sval)
{
    if (!sem)
        return -1;

    uint32_t value = sem->value;
    *sval = (value & SEM_VALUE_MASK) > SEM_VALUE_MAX
                ? -(value & SEM_WAIT_QUEUE_INDEX_MASK)
                : (SEM_VALUE_MAX - (value & SEM_VALUE_MASK));
    return 0;
}
