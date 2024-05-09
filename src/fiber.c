#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include "fiber.h"

#define _THREAD_STACK 1024 * 32
#define U_THREAD_MAX 16
#define K_THREAD_MAX 4
#define K_CONTEXT_MASK 0b11 /* bitmask for native thread ID */
#define PRIORITY 16
#define TIME_QUANTUM 50000 /* in us */

/* user-level thread control block (TCB) */
struct _tcb_internal {
    fiber_t tid;         /* thread ID            */
    fiber_status status; /* thread status        */
    ucontext_t context;  /* thread contex        */
    uint prio;           /* thread priority      */
    list_node node;      /* thread node in queue */
    char stack[1];       /* thread stack pointer */
};

#define GET_TCB(ptr) \
    ((_tcb *) ((char *) (ptr) - (unsigned long long) (&((_tcb *) 0)->node)))

/* user-level thread queue */
static list_node thread_queue[PRIORITY];

/* current user-level thread context */
static list_node *cur_thread_node[K_THREAD_MAX];

/* native thread context */
static ucontext_t context_main[K_THREAD_MAX];

/* number of active threads */
static int user_thread_num = 0;

static __thread int preempt_disable_count = 0;

/* global spinlock for critical section _queue */
static uint _spinlock = 0;

typedef struct {
    sem_t semaphore;
    unsigned long *val;
} sig_sem;

/* global semaphore for user-level thread */
static sig_sem sigsem_thread[U_THREAD_MAX];

/* timer management */
static struct itimerval time_quantum;
static struct itimerval zero_timer = {0};

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

/* diable schedule of native thread */
static inline void preempt_disable()
{
    __atomic_add_fetch(&preempt_disable_count, 1, __ATOMIC_ACQUIRE);
}

static inline void preempt_enable()
{
    __atomic_sub_fetch(&preempt_disable_count, 1, __ATOMIC_RELEASE);
}

static inline void spin_lock(uint *lock)
{
    preempt_disable();
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE))
        ;
}

static inline void spin_unlock(uint *lock)
{
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    preempt_enable();
}

static inline bool is_queue_empty(list_node *q)
{
    return (bool) (q->prev == q) && (q->next == q);
}

static inline void enqueue(list_node *q, list_node *node)
{
    node->next = q;
    q->prev->next = node;
    node->prev = q->prev;
    q->prev = node;
}

static inline bool dequeue(list_node *q, list_node **node)
{
    if (is_queue_empty(q))
        return false;

    *node = q->next;
    q->next = q->next->next;
    q->next->prev = q;

    (*node)->next = (*node)->prev = NULL;
    return true;
}

/* Fiber internals */

/* FIXME: avoid the use of global variables */
static int thread_nums = 0;

int fiber_init(int num)
{
    if (num <= 0)
        return -1;
    thread_nums = num; /* FIXME: validate the number of native threads */
    return 0;
}

void fiber_destroy()
{
    /* FIXME: destroy allocated resources */
    sleep(1);
}

static void k_thread_exec_func(void *arg);
static void u_thread_exec_func(void (*thread_func)(void *),
                               void *arg,
                               _tcb *thread);

/* create a new thread */
int fiber_create(fiber_t *tid, void (*start_func)(void *), void *arg)
{
    if (user_thread_num == U_THREAD_MAX) {
        /* exceed ceiling limit of user lever threads */
        perror("User level threads limit exceeded!");
        return -1;
    }

    /* create a TCB for the new thread */
    _tcb *thread = malloc(sizeof(_tcb) + 1 + _THREAD_STACK);
    if (!thread) {
        perror("Failed to allocate space for thread!");
        return -1;
    }

    /* prepare for first user-level thread */
    if (0 == user_thread_num) {
        /* Initialize time quantum */
        time_quantum.it_value.tv_sec = 0;
        time_quantum.it_value.tv_usec = TIME_QUANTUM;
        time_quantum.it_interval.tv_sec = 0;
        time_quantum.it_interval.tv_usec = TIME_QUANTUM;

        thread_queue->prev = thread_queue->next = thread_queue;

        for (int i = 0; i < thread_nums; i++) {
            /* allocate space for the newly created thread on stack */
            void *stack = malloc(_THREAD_STACK);
            if (!stack) {
                perror("Failed to allocate space for stack!");
                free(thread);
                return -1;
            }

            /* invoke the clone system call to create a native thread */
            if (-1 == clone((int (*)(void *)) k_thread_exec_func,
                            (char *) stack + _THREAD_STACK,
                            SIGCHLD | CLONE_SIGHAND | CLONE_VM | CLONE_PTRACE,
                            NULL)) {
                perror("Failed to invoke clone system call.");
                free(thread);
                free(stack);
                return -1;
            }
        }
    }

    /* set thread id and level */
    thread->tid = user_thread_num++;
    *tid = thread->tid;

    /* set initial priority to be the highest */
    thread->prio = 0;

    /* set node in thread run queue */
    thread->node.next = NULL;
    thread->node.prev = NULL;

    /* initialize sigsem_thread */
    sigsem_thread[thread->tid].val = NULL;
    sem_init(&(sigsem_thread[thread->tid].semaphore), 0, 0);

    /* create a context for this user-level thread */
    if (-1 == getcontext(&thread->context)) {
        perror("Failed to get uesr context!");
        return -1;
    }

    /* set the context to a newly allocated stack */
    thread->context.uc_link = &context_main[0];
    thread->context.uc_stack.ss_sp = thread->stack;
    thread->context.uc_stack.ss_size = _THREAD_STACK;
    thread->context.uc_stack.ss_flags = 0;

    /* set the context, which calls a wrapper function and then start_func */
    makecontext(&thread->context, (void (*)(void)) & u_thread_exec_func, 3,
                start_func, arg, thread);

    /* add newly created thread to the user-level thread run queue */
    spin_lock(&_spinlock);

    enqueue(thread_queue + thread->prio, &thread->node);
    spin_unlock(&_spinlock);

    return 0;
}

/* give CPU pocession to other user-level threads voluntarily */
int fiber_yield()
{
    uint k_tid = (uint) syscall(SYS_gettid);
    _tcb *cur_tcb = GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK]);
    spin_lock(&_spinlock);

    if (RUNNING == cur_tcb->status) {
        cur_tcb->status = SUSPENDED;
        enqueue(thread_queue + cur_tcb->prio,
                cur_thread_node[k_tid & K_CONTEXT_MASK]);
        spin_unlock(&_spinlock);

        swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
    } else
        spin_unlock(&_spinlock);
    return 0;
}

/* wait for thread termination */
int fiber_join(fiber_t thread, void **value_ptr)
{
    /* do P() in thread semaphore until the certain user-level thread is done */
    sem_wait(&(sigsem_thread[thread].semaphore));
    /* get the value's location passed to fiber_exit */
    if (value_ptr && sigsem_thread[thread].val)
        memcpy((unsigned long *) *value_ptr, sigsem_thread[thread].val,
               sizeof(unsigned long));
    return 0;
}

/* terminate a thread */
void fiber_exit(void *retval)
{
    uint k_tid = (uint) syscall(SYS_gettid);
    _tcb *cur_tcb = GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK]);
    fiber_t currefiber_id = cur_tcb->tid;

    cur_tcb->status = TERMINATED;
    /* When this thread finished, delete TCB and yield CPU control */
    user_thread_num--;

    sigsem_thread[currefiber_id].val = malloc(sizeof(unsigned long));
    memcpy(sigsem_thread[currefiber_id].val, retval, sizeof(unsigned long));

    spin_lock(&_spinlock);
    enqueue(thread_queue + cur_tcb->prio,
            cur_thread_node[k_tid & K_CONTEXT_MASK]);
    spin_unlock(&_spinlock);

    swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
}

/* schedule the user-level threads */
static void schedule()
{
    uint k_tid = (uint) syscall(SYS_gettid);
    _tcb *cur_tcb = GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK]);

    if (preempt_disable_count)
        return;

    spin_lock(&_spinlock);
    cur_tcb->status = SUSPENDED;
    enqueue(thread_queue + cur_tcb->prio,
            cur_thread_node[k_tid & K_CONTEXT_MASK]);
    spin_unlock(&_spinlock);

    swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
}

/* start user-level thread wrapper function */
static void u_thread_exec_func(void (*thread_func)(void *),
                               void *arg,
                               _tcb *thread)
{
    uint k_tid = 0;
    _tcb *u_thread = thread;

    thread_func(arg);
    u_thread->status = FINISHED;

    k_tid = (uint) syscall(SYS_gettid);

    u_thread->context.uc_link = &context_main[k_tid & K_CONTEXT_MASK];

    /* When this thread finished, delete TCB and yield CPU control */
    spin_lock(&_spinlock);
    enqueue(thread_queue + u_thread->prio,
            cur_thread_node[k_tid & K_CONTEXT_MASK]);
    spin_unlock(&_spinlock);

    swapcontext(&u_thread->context, &context_main[k_tid & K_CONTEXT_MASK]);
}

/* run native thread (or kernel-level thread) function */
static void k_thread_exec_func(void *arg UNUSED)
{
    uint k_tid = (uint) syscall(SYS_gettid);

    list_node *run_node = NULL;
    _tcb *run_tcb = NULL;

    /* timer and signal for user-level thread scheduling */
    struct sigaction sched_handler = {
        .sa_handler = &schedule, /* set signal handler to call scheduler */
    };
    sigaction(SIGPROF, &sched_handler, NULL);

    setitimer(ITIMER_PROF, &time_quantum, NULL);

    /* obtain and run a user-level thread from the user-level thread queue,
     * until no available user-level thread
     */
    while (1) {
        spin_lock(&_spinlock);

        if (!dequeue(thread_queue, &run_node)) {
            spin_unlock(&_spinlock);

            setitimer(ITIMER_PROF, &zero_timer, &time_quantum);
            return;
        }
        spin_unlock(&_spinlock);

        run_tcb = GET_TCB(run_node);

        /* current user thread is already terminated or finished by
         * fiber_exit()
         */
        if (TERMINATED == run_tcb->status || FINISHED == run_tcb->status) {
            /* do V() in thread semaphore implies that current user-level
             * thread is done.
             */
            sem_post(&(sigsem_thread[run_tcb->tid].semaphore));
            free(run_tcb);
            user_thread_num--;
            continue;
        }

        run_tcb->status = RUNNING;
        cur_thread_node[k_tid & K_CONTEXT_MASK] = run_node;
        swapcontext(&context_main[k_tid & K_CONTEXT_MASK], &(run_tcb->context));
    }
}

/* initialize the mutex lock */
int fiber_mutex_init(fiber_mutex_t *mutex)
{
    mutex->owner = NULL;
    mutex->lock = 0;

    (&(mutex->wait_list))->prev = &(mutex->wait_list);
    (&(mutex->wait_list))->next = &(mutex->wait_list);

    return 0;
}

/* acquire the mutex lock */
int fiber_mutex_lock(fiber_mutex_t *mutex)
{
    uint k_tid = (uint) syscall(SYS_gettid);

    /* avoid recursive locks */
    if (unlikely(mutex->owner ==
                 GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK])))
        return -1;

    /* Use "test-and-set" atomic operation to acquire the mutex lock */
    while (__atomic_test_and_set(&mutex->lock, __ATOMIC_ACQUIRE)) {
        enqueue(&mutex->wait_list, cur_thread_node[k_tid & K_CONTEXT_MASK]);
        swapcontext(
            &(GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK])->context),
            &context_main[k_tid & K_CONTEXT_MASK]);
    }
    mutex->owner = GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK]);

    return 0;
}

/* release the mutex lock */
int fiber_mutex_unlock(fiber_mutex_t *mutex)
{
    list_node *next_node = NULL;
    _tcb *cur_tcb = NULL;
    if (!dequeue(&(mutex->wait_list), &next_node)) {
        __atomic_store_n(&mutex->lock, 0, __ATOMIC_RELEASE);
        mutex->owner = NULL;
        return 0;
    }
    cur_tcb = GET_TCB(next_node);
    cur_tcb->prio = 0;
    spin_lock(&_spinlock);
    enqueue(thread_queue + cur_tcb->prio, next_node);
    spin_unlock(&_spinlock);
    __atomic_store_n(&mutex->lock, 0, __ATOMIC_RELEASE);
    mutex->owner = NULL;
    return 0;
}

/* destory the mutex lock */
int fiber_mutex_destroy(fiber_mutex_t *mutex UNUSED)
{
    /* FIXME: deallocate */
    return 0;
}

/* initial condition variable */
int fiber_cond_init(fiber_cond_t *condvar)
{
    (&(condvar->wait_list))->prev = &(condvar->wait_list);
    (&(condvar->wait_list))->next = &(condvar->wait_list);

    return 0;
}

/* wake up all threads on waiting list of condition variable */
int fiber_cond_broadcast(fiber_cond_t *condvar)
{
    list_node *next_node = NULL;
    _tcb *cur_tcb = NULL;
    while (!dequeue(&(condvar->wait_list), &next_node)) {
        cur_tcb = GET_TCB(next_node);
        cur_tcb->prio = 0;
        while (__atomic_test_and_set(&_spinlock, __ATOMIC_ACQUIRE))
            ;
        enqueue(thread_queue + cur_tcb->prio, next_node);
        __atomic_store_n(&_spinlock, 0, __ATOMIC_RELEASE);
    }
    return 0;
}

/* wake up a thread on waiting list of condition variable */
int fiber_cond_signal(fiber_cond_t *condvar)
{
    list_node *next_node = NULL;
    _tcb *cur_tcb = NULL;
    if (!dequeue(&(condvar->wait_list), &next_node))
        return 0;

    cur_tcb = GET_TCB(next_node);
    cur_tcb->prio = 0;
    spin_lock(&_spinlock);
    enqueue(thread_queue + cur_tcb->prio, next_node);
    spin_unlock(&_spinlock);

    return 0;
}

/* current thread go to sleep until other thread wakes it up */
int fiber_cond_wait(fiber_cond_t *condvar, fiber_mutex_t *mutex)
{
    uint k_tid = (uint) syscall(SYS_gettid);

    list_node *node = cur_thread_node[k_tid & K_CONTEXT_MASK];
    enqueue(&condvar->wait_list, node);

    fiber_mutex_unlock(mutex);
    swapcontext(&(GET_TCB(cur_thread_node[k_tid & K_CONTEXT_MASK])->context),
                &context_main[k_tid & K_CONTEXT_MASK]);
    fiber_mutex_lock(mutex);

    return 0;
}

/* destory condition variable */
int fiber_cond_destroy(fiber_cond_t *condvar UNUSED)
{
    /* FIXME: deallocate */
    return 0;
}
