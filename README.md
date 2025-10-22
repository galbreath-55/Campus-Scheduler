Campus Cloud Computing Center - Task Scheduler
Student: Evan Galbreath
Date: 10/21/2025

Implementation Summary:
	This program serves a simulation for four different scheduling algorithms such as those that may be used in an OS scheduler.
The program utilizes the C pthread library to ensure synchronization between multiple simulated tasks of varying types. Time is
simulated by using a set time unit and a shared clock_time which is updated as a thread runs. Tasks are placed into a shared
runqueue upon their arrival time, and the scheduler thread selects the desired thread to run next based on the user indicated
scheduling algorithm. Pthread condition signals are utilized to ensure that threads are notified when shared memory access is
availible to avoid deadlocks and busy waiting. Preemption is only utilized in the case of round robin scheduling, as each other
scheduling algorithm allows threads to run uninterrupted. Each thread is responsible for entering the queue when the clock time
reaches the simulated arrival time for that task. Upon entering the queue, the thread waits until it is signaled by the
scheduler. The scheduler waits until it has been signaled that the queue is not empty, and then schedules threads until the queue
is empty again. 


Algorithm Performance Table:

Algorithm	Avg Wait	Avg Turnaround		Avg Response	Best for
FCFS		11.04		12.92			11.04		BATCH
SJF		6.08		8.31			6.08		WEB
RR		11.25		13.54			3.67		STUDENT
PRI		8.31		10.65			8.31		REALTIME

Key findings:
Best for WEB: SJF becauuse WEB tasks are the shortest, and therefore complete fastest using this algorithm
Best for BATCH: FCFS becauuse Batch is low priority and long tasks so an algorithm that does not depend on either factor works the best.
Best for REALTIME: PRI because REALTIME tasks have the highest priority and therefore complete fastest using priority to sort.
Most Fair Overall: The overall most fair algorithm is RR as it gives each task an even time slice and rotates through giving each task a chance to complete some work each time.

Synchronization Design:
	Mutex and condition variables are used in this program to prevent multiple threads accessing the shared memory in the runqueue, and
time_clock. Each job has a unique condition variable which is called by the scheudler thread when it is being scheduled. By blocking at a
unique condition variable, the scheduler has control over exactly which task gets the mutex based off of the algorithm being ran currently.
The scheduler has a unique condition variable to regain access to the runqueue mutex once the task threads complete. Additionally,
the condition variable rq_not_empty exists so that threads can signal to the scheduler when a thread has entered the runqueue,
and allow the scheduling process to begin. The greatest challenge in creation of the thread synchonization here is managing
access to multiple shared memory points using only one mutex. Shared memory here includes the runqueue, but also the clock_time, and
reading this while another thread is attempting to write to it can cause memory problems and cause threads to execute out of order.
Additionally, it is important to ensure that each condition variable wait is within a while loop to prevent spurious wake ups and
illegal memory access. This is a challenge because creating while loop conditions which check for the condition variable being true
often attempt to access the memory that is being waited for. To get around this, additional fields for the job struct such as the
started and running booleans allow for synchronization checks without violating shared memory mutexes.
