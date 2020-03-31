#include <stdio.h>
#include "fiber_sem.h"

fiber_sem_t sem;

int g_value = 0;

void consume_func(task_t *task, void *data);

void product_func(task_t *fiber, void *data);

int main()
{
    fiber_sem_init(&sem, 0);
    pool_t *pl = create_pool(4);

    for (int i = 0; i < 256; ++i) {
        add_task(pl, create_task(consume_func, NULL));
        add_task(pl, create_task(product_func, NULL));
    }

    free_pool(pl);
    fiber_sem_destroy(&sem);
}

void consume_func(task_t *task, void *arg)
{
    (void) arg;
    int i;
    for (i = 0; i < 128; ++i) {
        fiber_sem_wait(task, &sem);
        printf("consume: value is %d\n", __sync_sub_and_fetch(&g_value, 1));
        yield_task(task);
    }
}

void product_func(task_t *fiber, void *arg)
{
    (void) arg;
    int i;
    for (i = 0; i < 128; ++i) {
        fiber_sem_post(&sem);
        printf("product: value is %d\n", __sync_add_and_fetch(&g_value, 1));
        yield_task(fiber);
    }
}
