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
    RR = 0, /**< round-robin */
    MLFQ,   /**< multi-level feedback queue */
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

/**
 * @brief Initialize Fiber internal data structure.
 *
 * @param num Specify the number of pre-allocated kernel-level thread, mapping
 *            beteen user-level thread and kernel-level implementation.
 */
int fiber_init(int num);
void fiber_destroy(void);

/**
 * @brief Create a new thread.
 */
int fiber_create(fiber_t *tid, void (*start_func)(void *), void *arg);

/**
 * @brief Yield the processor to other user level threads voluntarily.
 */
int fiber_yield();

/**
 * @brief Wait for thread termination.
 */
int fiber_join(fiber_t thread, void **value_ptr);

/**
 * @brief Terminate a thread.
 */
void fiber_exit(void *retval);

/**
 * @brief Initialize the mutex lock.
 */
int fiber_mutex_init(fiber_mutex_t *mutex);

/**
 * @brief Acquire the mutex lock.
 */
int fiber_mutex_lock(fiber_mutex_t *mutex);

/**
 * @brief Release the mutex lock.
 */
int fiber_mutex_unlock(fiber_mutex_t *mutex);

/**
 * @brief Destory the mutex lock.
 */
int fiber_mutex_destroy(fiber_mutex_t *mutex);

/**
 * @brief Initialize condition variable.
 */
int fiber_cond_init(fiber_cond_t *condvar);

/**
 * @brief Wake up all threads on waiting list.
 */
int fiber_cond_broadcast(fiber_cond_t *condvar);

/**
 * @brief Wake up a thread on waiting list.
 */
int fiber_cond_signal(fiber_cond_t *condvar);

/**
 * @brief Wait on a condition.
 * Current thread would go to sleep until other thread wakes it up.
 */
int fiber_cond_wait(fiber_cond_t *condvar, fiber_mutex_t *mutex);

/**
 * @brief Destory condition variable.
 */
int fiber_cond_destroy(fiber_cond_t *condvar);

#endif
