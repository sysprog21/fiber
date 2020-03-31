#include <stdio.h>
#include "fiber_mutex.h"

static fiber_mutex_t mtx;
static int g_val_array[8];

static void func(task_t *fiber, void *data)
{
    (void) data;
    for (int i = 0; i < 64; ++i) {
        fiber_mutex_lock(fiber, &mtx);
        for (int j = 0; j < 8; ++j)
            printf("%d ", ++g_val_array[j]);
        printf("\n");
        fiber_mutex_unlock(&mtx);
    }
}

int main()
{
    fiber_mutex_init(&mtx);
    pool_t *pl = create_pool(4);

    for (int i = 0; i < 1024; ++i)
        add_task(pl, create_task(func, NULL));

    free_pool(pl);
    fiber_mutex_destroy(&mtx);
}
