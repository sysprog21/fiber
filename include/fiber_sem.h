#ifndef __FIBER_SEMAPHORE_H
#define __FIBER_SEMAPHORE_H

#include "fiber.h"

typedef struct {
    volatile uint64_t value;
    task_t **wait_queue;
} fiber_sem_t;

int fiber_sem_init(fiber_sem_t *f_sem, int value);

int fiber_sem_destroy(fiber_sem_t *f_sem);

int fiber_sem_post(fiber_sem_t *f_sem);

int fiber_sem_wait(task_t *task, fiber_sem_t *f_sem);

int fiber_sem_getvalue(fiber_sem_t *f_sem, int *sval);

#endif
