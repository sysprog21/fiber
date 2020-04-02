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
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include "fiber.h"

#define _THREAD_STACK 1024 * 32
#define U_THREAD_MAX 16
#define K_THREAD_MAX 4
#define K_CONTEXT_MASK 0b11
#define PRIORITY 16
#define TIME_QUANTUM 50000

/* user_level Thread Control Block (TCB) */
struct _tcb_internal {
    fiber_t tid;         /* Thread ID            */
    fiber_status status; /* Thread Status        */
    ucontext_t context;  /* Thread Contex        */
    uint priority;       /* Thread Priority      */
    list_node node;      /* Thread Node in Queue */
    char stack[1];       /* Thread Stack pointer */
};

#define GET_TCB(ptr) \
    ((_tcb *) ((char *) (ptr) - (unsigned long long) (&((_tcb *) 0)->node)))

/* user level thread queue */
static list_node thread_queue[PRIORITY];

/* current user level Thread context (for user level thread only) */
static list_node *currefiber_node[K_THREAD_MAX];

/* kernel thread context */
static ucontext_t context_main[K_THREAD_MAX];

/* number of active threads */
static int user_thread_num = 0;

/* global spinlock for critical section _queue */
static uint _spinlock = 0;

typedef struct {
    sem_t semaphore;
    unsigned long *val;
} sig_semaphore;

/* global semaphore for user level thread */
static sig_semaphore sigsem_thread[U_THREAD_MAX];

/* timer and signal for user level thread scheduling */
static struct sigaction sched_handler;

static struct itimerval time_quantum;
static struct itimerval zero_timer = {0};

static int sched = 0;

/* Linked List Operations */

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

    (*node)->next = NULL;
    (*node)->prev = NULL;
    return true;
}

/* Fiber Operation */

static int thread_nums = 0;

int fiber_init(int num)
{
    if (num <= 0)
        return -1;
    thread_nums = num;
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
    _tcb *thread = (_tcb *) malloc(sizeof(_tcb) + 1 + _THREAD_STACK);

    if (0 == user_thread_num) {
        /* Initialize time quantum */
        time_quantum.it_value.tv_sec = 0;
        time_quantum.it_value.tv_usec = TIME_QUANTUM;
        time_quantum.it_interval.tv_sec = 0;
        time_quantum.it_interval.tv_usec = TIME_QUANTUM;

        thread_queue->prev = thread_queue;
        thread_queue->next = thread_queue;

        for (int i = 0; i < thread_nums; i++) {
            /* allocate space for the newly created thread on stack */
            void *stack = (void *) malloc(_THREAD_STACK);
            if (NULL == stack) {
                perror("Failed to allocate space for stack!");
                return -1;
            }

            /* invoke the clone system call to create a kernel thread */
            if (-1 == clone((int (*)(void *)) k_thread_exec_func,
                            (char *) stack + _THREAD_STACK,
                            SIGCHLD | CLONE_SIGHAND | CLONE_VM | CLONE_PTRACE,
                            NULL)) {
                perror("Failed to invoke clone system call.");
                free(stack);
                return -1;
            }
        }
    }

    /* set thread id and level */
    thread->tid = user_thread_num++;
    *tid = thread->tid;

    /* set initial priority to be the highest */
    thread->priority = 0;

    /* set node in thread run queue */
    thread->node.next = NULL;
    thread->node.prev = NULL;

    /* initialize sigsem_thread */
    sigsem_thread[thread->tid].val = NULL;
    sem_init(&(sigsem_thread[thread->tid].semaphore), 0, 0);

    /* create a context for this user level thread */
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

    /* add newly created thread to the user level thread run queue */
    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;

    enqueue(thread_queue + thread->priority, &thread->node);
    __sync_lock_release(&_spinlock);

    return 0;
}

/* give CPU pocession to other user level threads voluntarily */
int fiber_yield()
{
    uint k_tid = (uint) syscall(SYS_gettid);
    _tcb *cur_tcb = GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK]);
    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;

    if (RUNNING == cur_tcb->status) {
        cur_tcb->status = SUSPENDED;
        enqueue(thread_queue + cur_tcb->priority,
                currefiber_node[k_tid & K_CONTEXT_MASK]);
        __sync_lock_release(&_spinlock);

        swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
    } else
        __sync_lock_release(&_spinlock);
    return 0;
}

/* wait for thread termination */
int fiber_join(fiber_t thread, void **value_ptr)
{
    /* do P() in thread semaphore until the certain user level thread is done */
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
    _tcb *cur_tcb = GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK]);
    fiber_t currefiber_id = cur_tcb->tid;

    cur_tcb->status = TERMINATED;
    /* When this thread finished, delete TCB and yield CPU control */
    user_thread_num--;

    sigsem_thread[currefiber_id].val =
        (unsigned long *) malloc(sizeof(unsigned long));
    memcpy(sigsem_thread[currefiber_id].val, retval, sizeof(unsigned long));

    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;
    enqueue(thread_queue + cur_tcb->priority,
            currefiber_node[k_tid & K_CONTEXT_MASK]);
    __sync_lock_release(&_spinlock);

    swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
}

/* schedule the user level threads */
static void schedule()
{
    uint k_tid = (uint) syscall(SYS_gettid);
    _tcb *cur_tcb = GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK]);

    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;

    cur_tcb->status = SUSPENDED;

    if (MLFQ == sched && cur_tcb->priority < PRIORITY - 1)
        ++cur_tcb->priority;

    enqueue(thread_queue + cur_tcb->priority,
            currefiber_node[k_tid & K_CONTEXT_MASK]);

    __sync_lock_release(&_spinlock);

    swapcontext(&(cur_tcb->context), &context_main[k_tid & K_CONTEXT_MASK]);
}

/* start user level thread wrapper function */
static void u_thread_exec_func(void (*thread_func)(void *),
                               void *arg,
                               _tcb *thread)
{
    uint k_tid = 0;
    _tcb *u_thread = thread;

    u_thread->status = RUNNING;
    thread_func(arg);
    u_thread->status = FINISHED;

    k_tid = (uint) syscall(SYS_gettid);

    u_thread->context.uc_link = &context_main[k_tid & K_CONTEXT_MASK];

    /* When this thread finished, delete TCB and yield CPU control */
    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;
    enqueue(thread_queue + u_thread->priority,
            currefiber_node[k_tid & K_CONTEXT_MASK]);

    __sync_lock_release(&_spinlock);

    swapcontext(&u_thread->context, &context_main[k_tid & K_CONTEXT_MASK]);
}

/* run kernel level thread function */
static void k_thread_exec_func(void *arg)
{
    (void) arg;
    uint k_tid = (uint) syscall(SYS_gettid);

    list_node *run_node = NULL;
    _tcb *run_tcb = NULL;

    /* Set Signal Handler to Call Scheduler */
    memset(&sched_handler, 0, sizeof(sched_handler));
    sched_handler.sa_flags = SA_SIGINFO;
    sched_handler.sa_handler = &schedule;
    sigaction(SIGPROF, &sched_handler, NULL);

    setitimer(ITIMER_PROF, &time_quantum, NULL);

    /* obtain and run a user level thread from the user level thread queue,
     * until no available user level thread
     */
    while (1) {
        while (__sync_lock_test_and_set(&_spinlock, 1))
            ;

        if (!dequeue(thread_queue, &run_node)) {
            __sync_lock_release(&_spinlock);

            setitimer(ITIMER_PROF, &zero_timer, &time_quantum);
            return;
        }
        __sync_lock_release(&_spinlock);

        run_tcb = GET_TCB(run_node);

        /* current user thread is already terminated or finished by
         * fiber_exit()
         */
        if (TERMINATED == run_tcb->status || FINISHED == run_tcb->status) {
            /* do V() in thread semaphore implies current user level thread is
             * done */
            sem_post(&(sigsem_thread[run_tcb->tid].semaphore));
            free(run_tcb);
            user_thread_num--;
            continue;
        }

        run_tcb->status = RUNNING;
        currefiber_node[k_tid & K_CONTEXT_MASK] = run_node;
        swapcontext(&context_main[k_tid & K_CONTEXT_MASK], &(run_tcb->context));
    }
}

/* Mutual Exclusive Lock */

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
    /* Use "test-and-set" atomic operation to acquire the mutex lock */
    while (__sync_lock_test_and_set(&mutex->lock, 1)) {
        enqueue(&mutex->wait_list, currefiber_node[k_tid & K_CONTEXT_MASK]);
        swapcontext(
            &(GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK])->context),
            &context_main[k_tid & K_CONTEXT_MASK]);
    }
    mutex->owner = GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK]);

    return 0;
}

/* release the mutex lock */
int fiber_mutex_unlock(fiber_mutex_t *mutex)
{
    list_node *next_node = NULL;
    _tcb *cur_tcb = NULL;
    if (!dequeue(&(mutex->wait_list), &next_node)) {
        __sync_lock_release(&mutex->lock);
        mutex->owner = NULL;
        return 0;
    }
    cur_tcb = GET_TCB(next_node);
    cur_tcb->priority = 0;
    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;
    enqueue(thread_queue + cur_tcb->priority, next_node);
    __sync_lock_release(&_spinlock);
    __sync_lock_release(&mutex->lock);
    mutex->owner = NULL;
    return 0;
}

/* destory the mutex lock */
int fiber_mutex_destroy(fiber_mutex_t *mutex)
{
    /* FIXME: deallocate */
    (void) mutex;
    return 0;
}

/* Condition Variable */

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
        cur_tcb->priority = 0;
        while (__sync_lock_test_and_set(&_spinlock, 1))
            ;
        enqueue(thread_queue + cur_tcb->priority, next_node);
        __sync_lock_release(&_spinlock);
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
    cur_tcb->priority = 0;
    while (__sync_lock_test_and_set(&_spinlock, 1))
        ;
    enqueue(thread_queue + cur_tcb->priority, next_node);
    __sync_lock_release(&_spinlock);

    return 0;
}

/* current thread go to sleep until other thread wakes it up */
int fiber_cond_wait(fiber_cond_t *condvar, fiber_mutex_t *mutex)
{
    uint k_tid = (uint) syscall(SYS_gettid);

    list_node *node = currefiber_node[k_tid & K_CONTEXT_MASK];
    enqueue(&condvar->wait_list, node);

    fiber_mutex_unlock(mutex);
    swapcontext(&(GET_TCB(currefiber_node[k_tid & K_CONTEXT_MASK])->context),
                &context_main[k_tid & K_CONTEXT_MASK]);
    fiber_mutex_lock(mutex);

    return 0;
}

/* destory condition variable */
int fiber_cond_destroy(fiber_cond_t *condvar)
{
    /* FIXME: deallocate */
    (void) condvar;
    return 0;
}
