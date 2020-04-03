# fiber: A User Space Threading Library

Fiber is a lightweight thread library with M:N mapping between user-level
thread and kernel-level thread.

## Features
* Preemptive user threads
* Familiar threading concepts are available
  - Mutexes
  - Condition varialbles

## Implementation Details

The preemptive scheduler is implemented through timer and signal functions.
In k_thread_exec_func() function, a timer is initiated through the following:
```c
setitimer(ITIMER_PROF, &time_quantum, NULL)
```

When the timer expires, signal SIGPROF is sent to the process.
sigaction() would invoke the scheduling routine `schedule()` to run, which
chooses a thread from a run queue to run. The scheduler maintains a run queue.
The new created threads are pushedt into the end of the queue. The first thread in
the head of the queue is the thread currently running. Each time the scheduler
receives signal SIGPROF, it interrupts the running thread at the head by pushing
this thread into the end of the queue, swap the context between this thread and
next thread which is the new head of the queue.


## License
`fiber` is released under the MIT License. Use of this source code is governed
by a MIT License that can be found in the LICENSE file.
