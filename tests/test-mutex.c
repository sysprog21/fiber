#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "fiber.h"

static fiber_mutex_t mtx;
static int g_val_array[8];

static void func(void *data)
{
    (void) data;
    for (int i = 0; i < 64; ++i) {
        fiber_mutex_lock(&mtx);
        for (int j = 0; j < 8; ++j)
            printf("%d ", ++g_val_array[j]);
        printf("\n");
        /* recursive locks are not allowed */
        assert(fiber_mutex_lock(&mtx) == -1);
        fiber_mutex_unlock(&mtx);
    }
}

int main()
{
    fiber_init(1);
    fiber_mutex_init(&mtx);

    fiber_t thread[16];
    for (int i = 0; i < 16; ++i)
        fiber_create(&thread[i], &func, NULL);

    fiber_mutex_destroy(&mtx);
    fiber_destroy();
    return 0;
}
