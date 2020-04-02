#include <stdio.h>
#include <stdlib.h>

#include "fiber.h"

static fiber_mutex_t mtx;
static fiber_cond_t cond;

static int i = 0;

static void func1(void *data)
{
    (void) data;
    for (i = 1; i <= 6; i++) {
        fiber_mutex_lock(&mtx);
        printf("thread1: lock %d\n", __LINE__);
        if (i % 3 == 0) {
            printf("thread1:signal 1  %d\n", __LINE__);
            fiber_cond_signal(&cond);
            printf("thread1:signal 2  %d\n", __LINE__);
        }
        fiber_mutex_unlock(&mtx);
        printf("thread1: unlock %d\n\n", __LINE__);
        fiber_yield();
    }
}

static void func2(void *data)
{
    (void) data;
    while (i < 6) {
        fiber_mutex_lock(&mtx);
        printf("thread2: lock %d\n", __LINE__);
        if (i % 3 != 0) {
            printf("thread2: wait 1  %d\n", __LINE__);
            fiber_cond_wait(&cond, &mtx);
            printf("thread2: wait 2  %d\n", __LINE__);
        }
        fiber_mutex_unlock(&mtx);
        printf("thread2: unlock %d\n\n", __LINE__);
        fiber_yield();
    }
}

int main()
{
    fiber_init(2);
    fiber_mutex_init(&mtx);
    fiber_cond_init(&cond);

    fiber_t t1, t2;
    fiber_create(&t1, func1, NULL);
    fiber_create(&t2, func2, NULL);

    fiber_cond_destroy(&cond);
    fiber_mutex_destroy(&mtx);
    fiber_destroy();
    return 0;
}
