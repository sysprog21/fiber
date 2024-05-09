/*
 * Purpose: the use of getcontext, swapcontext, makecontext, etc.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

static ucontext_t ping_ctx, pong_ctx;
static double p = 0.9;
static void ping()
{
    puts("Ping!");
    fflush(stdout);

    while (rand() / (RAND_MAX + 1.0) < p) {
        puts("  ping >");
        swapcontext(&ping_ctx, &pong_ctx);
    }
}

static void pong()
{
    puts("Pong!");
    fflush(stdout);

    while (rand() / (RAND_MAX + 1.0) < p) {
        puts("< pong");
        swapcontext(&pong_ctx, &ping_ctx);
    }
}

int main()
{
    ucontext_t main_ctx;

    srand(getpid());

    /* getcontext must be called on a context object before makecontext */
    if (getcontext(&ping_ctx) == -1 || getcontext(&pong_ctx) == -1) {
        perror("getcontext");
        exit(EXIT_FAILURE);
    }

    /* Allocate a new stacks */
    ping_ctx.uc_stack.ss_sp = malloc(SIGSTKSZ);
    ping_ctx.uc_stack.ss_size = SIGSTKSZ;

    pong_ctx.uc_stack.ss_sp = malloc(SIGSTKSZ);
    pong_ctx.uc_stack.ss_size = SIGSTKSZ;

    /* Set the successor context */
    ping_ctx.uc_link = &main_ctx;
    pong_ctx.uc_link = &main_ctx;

    /* makecontext sets the starting routine for the context */
    makecontext(&ping_ctx, ping, 1, &p);
    makecontext(&pong_ctx, pong, 1, &p);

    /* ping serves. When we switch back to main_ctx, control resumes here. */
    if (swapcontext(&main_ctx, &ping_ctx) == -1) {
        perror("swapcontext");
        exit(EXIT_FAILURE);
    }

    free(ping_ctx.uc_stack.ss_sp);
    free(pong_ctx.uc_stack.ss_sp);

    printf("main: exiting\n");
    return 0;
}
