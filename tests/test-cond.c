#include <stdio.h>
#include <stdlib.h>

#include "fiber_cond.h"

static fiber_mutex_t mtx;
static fiber_cond_t cond;

static int i = 0;

static void func1(task_t *task, void *data)
{
    (void) data;
    for (i = 1; i <= 6; i++) {
        fiber_mutex_lock(task, &mtx);
        printf("thread1: lock %d\n", __LINE__);
        if (i % 3 == 0) {
            printf("thread1:signal 1  %d\n", __LINE__);
            fiber_cond_signal(&cond);
            printf("thread1:signal 2  %d\n", __LINE__);
        }
        fiber_mutex_unlock(&mtx);
        printf("thread1: unlock %d\n\n", __LINE__);
        yield_task(task);
    }
}

static void func2(task_t *task, void *data)
{
    (void) data;
    while (i < 6) {
        fiber_mutex_lock(task, &mtx);
        printf("thread2: lock %d\n", __LINE__);
        if (i % 3 != 0) {
            printf("thread2: wait 1  %d\n", __LINE__);
            fiber_cond_wait(task, &cond, &mtx);
            printf("thread2: wait 2  %d\n", __LINE__);
        }
        fiber_mutex_unlock(&mtx);
        printf("thread2: unlock %d\n\n", __LINE__);
        yield_task(task);
    }
}

int main()
{
    fiber_mutex_init(&mtx);
    fiber_cond_init(&cond);

    pool_t *pl = create_pool(1);
    if (!pl)
        exit(-1);
    add_task(pl, create_task(func1, NULL));
    add_task(pl, create_task(func2, NULL));
    free_pool(pl);

    fiber_cond_destroy(&cond);
    fiber_mutex_destroy(&mtx);

    return 0;
}
