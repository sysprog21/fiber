#ifndef __FIBER_COND_H
#define __FIBER_COND_H

#include "fiber_mutex.h"

typedef struct {
    volatile uint64_t value;
    task_t **wait_queue;
} fiber_cond_t;

int fiber_cond_init(fiber_cond_t *cond);

int fiber_cond_destroy(fiber_cond_t *cond);

int fiber_cond_signal(fiber_cond_t *cond);

int fiber_cond_broadcast(fiber_cond_t *cond);

int fiber_cond_wait(task_t *task, fiber_cond_t *cond, fiber_mutex_t *mtx);

#endif
