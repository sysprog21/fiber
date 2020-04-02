#ifndef FIBER_H
#define FIBER_H

#include <stdint.h>

typedef uint fiber_t;

/* Task Linked List */
typedef struct list_node {
    struct list_node *next, *prev;
} list_node;

/* Fiber status */
typedef enum {
    NOT_STARTED = 0,
    RUNNING,
    SUSPENDED,
    TERMINATED,
    FINISHED,
} fiber_status;

typedef enum {
    RR = 0,
    MLFQ, /* multi-level feedback queue */
} fiber_sched_policy;

/* user_level Thread Control Block (TCB) */
typedef struct _tcb_internal _tcb;

typedef struct {
    _tcb *owner;
    uint lock;
    list_node wait_list;
} fiber_mutex_t;

typedef struct {
    list_node wait_list;
    fiber_mutex_t list_mutex;
} fiber_cond_t;

int fiber_init(int num);
void fiber_destroy(void);

/* create a new thread */
int fiber_create(fiber_t *tid, void (*start_func)(void *), void *arg);

/* give CPU pocession to other user level threads voluntarily */
int fiber_yield();

/* wait for thread termination */
int fiber_join(fiber_t thread, void **value_ptr);

/* terminate a thread */
void fiber_exit(void *retval);

/* initial the mutex lock */
int fiber_mutex_init(fiber_mutex_t *mutex);

/* acquire the mutex lock */
int fiber_mutex_lock(fiber_mutex_t *mutex);

/* release the mutex lock */
int fiber_mutex_unlock(fiber_mutex_t *mutex);

/* destory the mutex lock */
int fiber_mutex_destroy(fiber_mutex_t *mutex);

/* initialize condition variable */
int fiber_cond_init(fiber_cond_t *condvar);

/* wake up all threads on waiting list */
int fiber_cond_broadcast(fiber_cond_t *condvar);

/* wake up a thread on waiting list */
int fiber_cond_signal(fiber_cond_t *condvar);

/* current thread go to sleep until other thread wakes it up */
int fiber_cond_wait(fiber_cond_t *condvar, fiber_mutex_t *mutex);

/* destory condition variable */
int fiber_cond_destroy(fiber_cond_t *condvar);

#endif
