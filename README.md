# fiber: A User Space Threading Library

Fiber is a lightweight thread library with M:N mapping between user-level
thread (ULT) and Linux native thread (or kernel-level thread, KLT).

## Features
* Preemptive user-level threads
* Familiar threading concepts are available
  - Mutexes
  - Condition variables

## Implementation Details

The preemptive scheduler is implemented through timer and signal functions.
In `k_thread_exec_func()` function, a timer is initiated through the following:
```c
setitimer(ITIMER_PROF, &timeslice, NULL)
```

When the timer expires, signal `SIGPROF` is sent to the process.
`sigaction()` would invoke the scheduling routine `schedule()` to run, which
chooses a thread from a run queue to run. The scheduler maintains a run queue.
The new created threads are pushed into the end of the queue. The first thread
in the head of the queue is the thread currently running. Each time the
scheduler receives signal `SIGPROF`, it interrupts the running thread at the
head by pushing this thread into the end of the queue. In addition, it swaps
the context between this thread and next thread which is the new head of the
queue.

A userspace program/process may not create a kernel thread. Instead, it could
create a *native* thread using `pthread_create`, which invokes the `clone`
system call to do so. Inside Fiber, `clone` system call is used for creating
kernel-level threads. With a kernel that understands threads, we use `clone`,
but we still have to create the new thread's stack. The kernel does not
create/assign a stack for a new thread. The `clone` system call accepts a
`child_stack` argument. Therefore, `fiber_create` must allocate a stack for
the new thread and pass that to clone:
```c
    /* invoke the clone system call to create a native thread */
    if (-1 == clone((int (*)(void *)) k_thread_exec_func,
                    (char *) stack + _THREAD_STACK,
                    SIGCHLD | CLONE_SIGHAND | CLONE_VM | CLONE_PTRACE, NULL)) {
        perror("Failed to invoke clone system call.");
        ...
    }
```

Only a process or main thread is assigned its initial stack by the kernel,
usually at a high memory address. Thus, if the process does not use threads,
normally, it just uses that pre-assigned stack. But, if a thread is created,
i.e., the  native thread, the starting process/thread must pre-allocate the
area for the proposed thread with malloc.

## License
`fiber` is released under the MIT License. Use of this source code is governed
by a MIT License that can be found in the LICENSE file.
