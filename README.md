# Campus Cloud Computing Center — Task Scheduler Report
---

## Implementation Summary

This project implements a simulation of four scheduling algorithms commonly used in operating system schedulers. The program is written in C and leverages the POSIX `pthread` library to ensure proper synchronization between multiple simulated tasks of varying types.

Time is simulated using a fixed time unit and a shared `clock_time` variable, which is updated as threads execute. Tasks enter a shared runqueue once their simulated arrival time is reached. A dedicated scheduler thread selects the next task to execute based on the given scheduling algorithm.

Pthread condition variables are used to notify threads when shared memory access is available, preventing deadlocks and busy waiting. Preemption is only implemented for Round Robin scheduling because all other algorithms allow tasks to run uninterrupted. Each task thread is runqueue at its arrival time and then blocks until signaled by the scheduler. The scheduler itself blocks until notified that the run queue is non-empty, after which it schedules tasks until the queue is empty again.

---

## Algorithm Performance

| Algorithm | Avg Wait | Avg Turnaround | Avg Response | Best For |
| --------- | -------- | -------------- | ------------ | -------- |
| FCFS      | 11.04    | 12.92          | 11.04        | Batch    |
| SJF       | 6.08     | 8.31           | 6.08         | Web      |
| RR        | 11.25    | 13.54          | 3.67         | Student  |
| PRI       | 8.31     | 10.65          | 8.31         | Realtime |

---

## Key Findings

* **Best for Web Workloads:** *Shortest Job First (SJF)* performs best because web tasks tend to be short-lived and complete fastest when shorter jobs are prioritized.

* **Best for Batch Workloads:** *First Come First Serve (FCFS)* is most effective for batch processing, as these tasks are typically long-running and low priority, making a simple, non-preemptive approach suitable.

* **Best for Realtime Workloads:** *Priority Scheduling (PRI)* is optimal for realtime tasks since it prioritizes high-importance jobs, allowing them to complete as quickly as possible.

* **Most Fair Overall:** *Round Robin (RR)* provides the most fairness by assigning equal time slices to each task and rotating through them, ensuring steady progress for all jobs.

---

## Synchronization Design

The program uses a combination of mutexes and condition variables to manage access to shared resources such as the run queue and the simulated clock. A single mutex protects all shared memory, including the run queue and `clock_time`, to prevent race conditions and inconsistent state.

Each job has its own condition variable, which is signaled by the scheduler when that job is selected to run. This design allows the scheduler to precisely control which task acquires the mutex based on the active scheduling algorithm. The scheduler itself also uses a dedicated condition variable to regain access to the run queue once a task finishes execution.

An additional condition variable, `rq_not_empty`, enables tasks to notify the scheduler when they enter the run queue, triggering the scheduling process.

One of the primary challenges in this design is managing multiple shared memory locations with a single mutex. In particular, concurrent access to `clock_time` can cause tasks to execute out of order if not properly synchronized. Another challenge is ensuring that all condition variable waits are enclosed within `while` loops to guard against spurious wakeups and illegal memory access.

To address these issues, additional fields in the job structure—such as `started` and `running` boolean flags—are used. These fields allow threads to perform synchronization checks safely without violating mutex constraints, ensuring correct execution order and system stability.

---

## Conclusion

This project demonstrates the behavior, trade-offs, and performance implications of several classic CPU scheduling algorithms. Through careful synchronization design and controlled simulation of time, the implementation highlights how scheduling strategies impact fairness, responsiveness, and efficiency across different workload types.
