#ifndef __FIBER_MUTEX_H
#define __FIBER_MUTEX_H

#include "fiber.h"

typedef struct {
    volatile uint64_t value;
    task_t **wait_queue;
} fiber_mutex_t;

int fiber_mutex_init(fiber_mutex_t *mtx);

int fiber_mutex_destroy(fiber_mutex_t *mtx);

int fiber_mutex_lock(task_t *fiber, fiber_mutex_t *mtx);

int fiber_mutex_unlock(fiber_mutex_t *mtx);

#endif
