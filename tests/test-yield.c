#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "fiber.h"

static int a[0xFFFF][8];

static void test_yield(void *arg)
{
    char *t_name = (char *) arg;
    fprintf(stdout, "User Level Thread \"%s\" start !\n", t_name);
    for (int i = 0; i < 0xFFFF; i++) {
        if (i == 0)
            fprintf(stdout,
                    "User Level Thread \"%s\" is running in tid = %d \n",
                    t_name, (int) syscall(SYS_gettid));
    }
    fprintf(stdout, "User Level Thread \"%s\" pause !\n", t_name);
    fiber_yield();
    fprintf(stdout, "User Level Thread \"%s\" resume !\n", t_name);
    for (int i = 0; i < 0xFFFF; i++) {
        if (i == 0)
            fprintf(stdout,
                    "User Level Thread \"%s\" is running in tid = %d \n",
                    t_name, (int) syscall(SYS_gettid));
    }
    fprintf(stdout, "User Level Thread \"%s\" finish !\n", t_name);
}

static void calc(void *arg)
{
    char *t_name = (char *) arg;
    int row = atoi(t_name) - 1;
    int sum = 0;
    int i = 0, j = 0, k = 0;
    fprintf(stdout, "User Level Thread \"%s\" start in tid = %d !\n", t_name,
            (int) syscall(SYS_gettid));
    while (i < 0x7FFF) {
        sum += a[i][row];
        i++;
        /* Just do some thing to make it runs longer */
        while (j < 0xFFFFFFF) {
            j += 2;
            j -= 1;
            if (j % 0xFFFFFF == 0)
                fprintf(stdout,
                        "User Level Thread \"%s\" is running in tid = %d !\n",
                        t_name, (int) syscall(SYS_gettid));
        }
    }
    fprintf(stdout, "User Level Thread \"%s\" pause !\n", t_name);
    fiber_yield();
    fprintf(stdout, "User Level Thread \"%s\" resume in tid = %d !\n", t_name,
            (int) syscall(SYS_gettid));
    while (i < 0xFFFF) {
        sum += a[i][row];
        i++;
        /* Just do some thing to make it runs longer */
        while (k < 0xFFFFFFF) {
            k += 2;
            k -= 1;
            if (k % 0xFFFFFF == 0)
                fprintf(stdout,
                        "User Level Thread \"%s\" is running in tid = %d !\n",
                        t_name, (int) syscall(SYS_gettid));
        }
    }
    fprintf(stdout, "User Level Thread \"%s\" finish ! sum[%d] = %d\n", t_name,
            row, sum);
}

int main()
{
    fiber_t u1, u2, u3, u4;

    for (int j = 0; j < 0xFFFF; j++)
        for (int i = 0; i < 8; i++)
            a[j][i] = i;

    fiber_init(2);

    fiber_create(&u1, &test_yield, "1");
    fiber_create(&u2, &test_yield, "2");
    fiber_create(&u3, &calc, "3");
    fiber_create(&u4, &calc, "4");

    fprintf(stdout, "Main Thread Starts !\n");

    fiber_join(u1, NULL);
    fiber_join(u2, NULL);
    fiber_join(u3, NULL);
    fiber_join(u4, NULL);

    fiber_destroy();

    fprintf(stdout, "Main Thread Ends !\n");

    return 0;
}
