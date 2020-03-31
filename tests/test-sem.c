#include <stdio.h>
#include <stdlib.h>

#include "fiber_sem.h"

static fiber_sem_t sem;

static int g_value = 0;

static void consumer_func(task_t *task, void *arg)
{
    (void) arg;
    for (int i = 0; i < 128; ++i) {
        fiber_sem_wait(task, &sem);
        printf("consumer: value is %d\n", __sync_sub_and_fetch(&g_value, 1));
        yield_task(task);
    }
}

static void producer_func(task_t *fiber, void *arg)
{
    (void) arg;
    for (int i = 0; i < 128; ++i) {
        fiber_sem_post(&sem);
        printf("producer: value is %d\n", __sync_add_and_fetch(&g_value, 1));
        yield_task(fiber);
    }
}

int main()
{
    fiber_sem_init(&sem, 0);
    pool_t *pl = create_pool(4);
    if (!pl)
        exit(-1);

    for (int i = 0; i < 256; ++i) {
        add_task(pl, create_task(consumer_func, NULL));
        add_task(pl, create_task(producer_func, NULL));
    }

    free_pool(pl);
    fiber_sem_destroy(&sem);

    return 0;
}
