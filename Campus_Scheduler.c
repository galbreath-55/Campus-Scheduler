/*
* CSE 2431/5431 – Campus Cloud Computing Center Scheduler (Lab 2)
* Student Starter Template (fill TODOs)
*
* Build on COELinux:
* gcc -Wall -g -pthread -o campus_scheduler campus_scheduler.c
*
* Notes:
* - Use "PRI" (not "PRIORITY") on the command line for Priority scheduling.
* - _DEFAULT_SOURCE enables POSIX interfaces like usleep() from unistd.h.

*/
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

/* ---------- simulation constants ---------- */
#define NUM_JOBS 12
#define QSIZE 32
#define QUANTUM 3 /* RR time slice in simulation units */
#define UNIT_MS 100000 /* 1 time-unit = 0.1 s (100 ms) */

/* ---------- job types ---------- */
typedef enum { BATCH=0, WEB=1, REALTIME=2, STUDENT=3 } JobType;
static const char *type_name[] = { "BATCH", "WEB", "REALTIME", "STUDENT" };

/* ---------- job and queue structures ---------- */
typedef struct {
	int job_id;
	JobType type;
	int arrival; /* submit time */
	int burst; /* total CPU demand */
	int priority; /* 1 = highest */

	/* runtime fields (set/updated by scheduler + jobs) */
	int remain; /* remaining time */
	int start; /* time first started (-1 if none) */
	int finish; /* time completed (-1 if none) */
	bool started; /* first dispatch recorded */

	/* thread sync */
	pthread_cond_t cv; /* scheduler wakes this job */
	int slice; /* time-units granted this turn */
	bool running; /* true while job is burning CPU */

	/* analytics */
	int wait_time;
	int turnaround;
	int response_time;
} Job;

typedef struct {
	Job* buf[QSIZE];
	int head, tail, count;
} Queue;

/* ---------- global scheduler state ---------- */
static Queue rq;
static pthread_mutex_t rq_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rq_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t sched_cv = PTHREAD_COND_INITIALIZER;
static int clock_time = 0; /* simulated time */
static int finished = 0; /* completed jobs */

/* ---------- queue helpers (students complete) ---------- */
static void q_init() {
	rq.head = rq.tail = rq.count = 0;
}

static void q_push(Job *j){
	/* Insert at tail of circular buffer; update tail and count */
	rq.buf[rq.tail] = j;
	rq.tail = (rq.tail+1)%QSIZE;
	rq.count++;
}

static Job* q_pop_head(){
	/* remove from head of circular buffer; update head and count; return item */
	Job *j = rq.buf[rq.head];
	rq.head = (rq.head+1)%QSIZE;
	rq.count--;
	return j;
}

/* remove and return job with shortest remaining time (SJF helper) */
static Job* q_pop_shortest(){
	/* scan rq.buf over rq.count entries; choose min remain; compact buffer
	*/
	Job* shortest = rq.buf[rq.head];
	int index = rq.head;
	for(int k = 0; k < rq.count; k++) {
		int i = (rq.head + k) % QSIZE;
		Job* curr = rq.buf[i];
		if(curr->burst < shortest->burst) {
			shortest = curr;
			index = i;
		}
	}
	Job* temp = rq.buf[rq.head];
	rq.buf[rq.head] = shortest;
	rq.buf[index] = temp;
	return q_pop_head();
}

/* remove and return job with highest priority (lowest number) */
static Job* q_pop_highest_pri(){
	/* scan rq.buf over rq.count entries; choose lowest j->priority; compact
	buffer */
	Job* highest = rq.buf[rq.head];
	int index = rq.head;
	for(int k = 0; k < rq.count; k++) {
		int i = (rq.head + k) % QSIZE;
		Job* curr = rq.buf[i];
		if(curr->priority < highest->priority) {
			highest = curr;
			index = i;
		}
	}
	Job* temp = rq.buf[rq.head];
	rq.buf[rq.head] = highest;
	rq.buf[index] = temp;
	return q_pop_head();
}

/* ---------- prototypes students must implement ---------- */
static void init_scheduler(Job jobs[], int n);
static void cleanup_scheduler(Job jobs[], int n);
static void* job_thread(void* arg);
static void simulate_work(int time_units);
static void* fcfs_scheduler(void* arg);
static void* sjf_scheduler(void* arg);
static void* rr_scheduler(void* arg);
static void* priority_scheduler(void* arg);
static void calculate_metrics(Job jobs[], int n);
static void print_results(Job jobs[], int n, const char* algorithm);

/* ---------- job set (provided) ---------- */
static Job job_templates[NUM_JOBS] = {

	/* BATCH: longer CPU-bound */
	{0,BATCH,0,15,3,15,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{1,BATCH,2,20,3,20,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{2,BATCH,5,25,3,25,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},

	/* WEB: short I/O-like */
	{3,WEB,1,3,2,3,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{4,WEB,3,2,2,2,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{5,WEB,4,4,2,4,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},

	/* REALTIME: highest priority */
	{6,REALTIME,0,5,1,5,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{7,REALTIME,2,3,1,3,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{8,REALTIME,6,4,1,4,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},

	/* STUDENT: mixed */
	{9,STUDENT,1,8,4,8,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{10,STUDENT,3,6,4,6,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
	{11,STUDENT,7,12,4,12,-1,-1,false,PTHREAD_COND_INITIALIZER,0,false},
};

int main(int argc, char* argv[]){
	if (argc != 2){
		fprintf(stderr, "Usage: %s <FCFS|SJF|RR|PRI>\n", argv[0]);
		return 1;
	}

	printf("OSU Campus Cloud Computing Center - Task Scheduler\n");
	printf("Algorithm: %s\nSimulating 12 computational jobs...\n", argv[1]);

	const char* alg = argv[1];
	Job jobs[NUM_JOBS];
	memcpy(jobs, job_templates, sizeof(job_templates));
	for(int i=0;i<NUM_JOBS;i++){
		jobs[i].remain = jobs[i].burst;
		jobs[i].start = -1;
		jobs[i].finish = -1;
		jobs[i].started = false;
		jobs[i].slice = 0;
		jobs[i].running = false;
		pthread_cond_init(&jobs[i].cv, NULL);
	}

	init_scheduler(jobs, NUM_JOBS);
	/* create job threads */
	pthread_t tids[NUM_JOBS];
	for(int i=0;i<NUM_JOBS;i++){
		pthread_create(&tids[i], NULL, job_thread, &jobs[i]);
	}

	/* create scheduler thread */
	pthread_t sched;
	if (strcmp(alg,"FCFS")==0)
		pthread_create(&sched,NULL,fcfs_scheduler,jobs);
	else if (strcmp(alg,"SJF")==0) pthread_create(&sched,NULL,sjf_scheduler,jobs);
	else if (strcmp(alg,"RR")==0) pthread_create(&sched,NULL,rr_scheduler,jobs);
	else if (strcmp(alg,"PRI")==0)
		pthread_create(&sched,NULL,priority_scheduler,jobs);
	else {
		fprintf(stderr, "Invalid algorithm: %s\n", alg);
		return 1;
	}

	for(int i=0;i<NUM_JOBS;i++) pthread_join(tids[i], NULL);
	pthread_join(sched, NULL);
	calculate_metrics(jobs, NUM_JOBS);
	print_results(jobs, NUM_JOBS, alg);
	cleanup_scheduler(jobs, NUM_JOBS);
	return 0;
}



/* ====================== STUDENT TODOS BELOW ====================== */
static void init_scheduler(Job jobs[], int n){
	/* Initialize global state, queue, and any additional sync if needed */
	q_init();
	clock_time = 0;
	finished = 0;
}

static void cleanup_scheduler(Job jobs[], int n){
	/* Destroy per-job condition variables if dynamically initialized */
	for(int i=0;i<n;i++) pthread_cond_destroy(&jobs[i].cv);
}

/*
* Each job should:
* 1) Wait until its arrival time relative to the simulated clock.
* 2) Join the ready queue with mutex protection and signal rq_not_empty.
* 3) Block on its own condition variable until scheduled.
* 4) When signaled, run for 'slice' time units (simulate_work), update
remain/clock.
* 5) On completion, set finish, increment finished, and signal scheduler.
*/

static void* job_thread(void* arg){
	Job* j = (Job*)arg;
	/* Implement arrival waiting, enqueue, run-loop, and completion signaling
	*/
	while(clock_time != j->arrival); //wait for simulated arrival time
	printf("[%d] %s Job %d submitted (P%d, Burst %d).\n", clock_time, type_name[j->type], j->job_id, j->priority, j->burst);
	q_push(j); //enter runqueue
	if(rq.count == 1) pthread_cond_signal(&rq_not_empty);
	pthread_mutex_unlock(&rq_mtx);

	while(j->finish == -1) { //repeat this section until completion.
		while(!j->running) {
			pthread_cond_wait(&j->cv, &rq_mtx);
		}
		printf("[%d] %s Job %d started execution.\n", clock_time, type_name[j->type], j->job_id);
		pthread_mutex_unlock(&rq_mtx);
		simulate_work(MIN(j->remain, j->slice)); //work until finished or slice is up
		pthread_mutex_lock(&rq_mtx);
		j->remain = j->remain - MIN(j->remain, j->slice);
		if(j->remain == 0) {
			j->finish = clock_time;
			finished++;
			printf("[%d] %s Job %d completed.\n", clock_time, type_name[j->type], j->job_id);
		}
		j->running = false;
		pthread_cond_signal(&sched_cv);
		pthread_mutex_unlock(&rq_mtx); //after timeslice or completion, signal scheduler
	}

	return NULL;
}

/* Simulate CPU work: block real time to represent 'time_units' of CPU */
static void simulate_work(int time_units){
	/* Loops through the given time_units, sleeps for the unit, and simulates clock_time */
	for(int i = 0; i < time_units; i++) {
		usleep(UNIT_MS);
		pthread_mutex_lock(&rq_mtx);
		clock_time++;
		pthread_cond_broadcast(&rq_not_empty);
		pthread_mutex_unlock(&rq_mtx);
	}
}

/*
* FCFS: Dequeue the head job, let it run to completion, repeat until all jobs
finish.
* Use rq_not_empty and sched_cv to coordinate with job threads.
*/
static void* fcfs_scheduler(void* arg){
	while(rq.count == 0) pthread_cond_wait(&rq_not_empty, &rq_mtx); //wait for the runqueue to not be empty

	while(rq.count != 0) {
		Job* j = q_pop_head();
		j->slice = j->remain;
		j->started = true;
		j->start = clock_time;

		j->running = true;
		pthread_cond_signal(&j->cv);
		pthread_mutex_unlock(&rq_mtx);

		while(j->running) pthread_cond_wait(&sched_cv, &rq_mtx); //wait for thread to complete
	}

	return NULL;
}

/*
* SJF (non-preemptive): Pick the ready job with shortest remaining time, run to
completion.
*/
static void* sjf_scheduler(void* arg){
	/* Implement non-preemptive SJF using q_pop_shortest */
	while(rq.count == 0) pthread_cond_wait(&rq_not_empty, &rq_mtx); //wait for the runqueue to not be empty

	while(rq.count != 0) {
		Job* j = q_pop_shortest();
		j->slice = j->remain;
		j->started = true;
		j->start = clock_time;

		j->running = true;
		pthread_cond_signal(&j->cv);
		pthread_mutex_unlock(&rq_mtx);

		while(j->running) pthread_cond_wait(&sched_cv, &rq_mtx); //wait for thread to complete
	}
	return NULL;
}

/*
* Round Robin: Time quantum = QUANTUM; requeue jobs that are not finished after a
slice.
*/
static void* rr_scheduler(void* arg){
	/* Implement RR using q_pop_head, per-job slice=MIN(remain, QUANTUM), and
	requeue */

	while(rq.count == 0) pthread_cond_wait(&rq_not_empty, &rq_mtx);

	while(rq.count != 0) {
		Job* j = q_pop_head();
		j->slice = QUANTUM;
		if(!j->started) j->start = clock_time;
		j->started = true;
		j->running = true;

		pthread_cond_signal(&j->cv);
		pthread_mutex_unlock(&rq_mtx);

		while(j->running) pthread_cond_wait(&sched_cv, &rq_mtx); //wait for thread to stop running
		if(j->remain != 0) q_push(j);
	}
	return NULL;
}

/*
* Priority: Pick highest-priority job (lowest priority number), run to completion.
* Optional: Aging or tie-breakers by arrival/job_id.
*/
static void* priority_scheduler(void* arg){
	/* Implement Priority using q_pop_highest_pri */

	while(rq.count == 0) pthread_cond_wait(&rq_not_empty, &rq_mtx);

	while(rq.count != 0) {
		Job* j = q_pop_highest_pri();
		j->slice = j->remain;
		j->started = true;
		j->start = clock_time;
		j->running = true;

		pthread_cond_signal(&j->cv);
		pthread_mutex_unlock(&rq_mtx);

		while(j->running) pthread_cond_wait(&sched_cv, &rq_mtx);
	}
	return NULL;
}

/*
* For this lab:
* wait_time = start - arrival
* turnaround = finish - arrival
* response_time = start - arrival
*/
static void calculate_metrics(Job jobs[], int n){
	for(int i = 0; i < n; i++) {
		Job* j = &jobs[i];
		if(j->wait_time == 0) j->wait_time = j->start - j->arrival; //if non-zero, round robin scheduler, wait time calculated by scheduler
		j->turnaround = j->finish - j->arrival;
		j->wait_time = j->turnaround - j->burst;
		j->response_time = j->start - j->arrival;
	}
}

/* Print per-job metrics and overall averages; optionally add per-type summaries.
*/
static void print_results(Job jobs[], int n, const char* algorithm){
	printf("JobID\tType     \tArrival\tBurst\tPri\tStart\tFinish\tWait\tTurn\tResp\n\n");
	double avgWaits[4];
	double avgResponse[4];
	double avgTurnaround[4];
        for(int i = 0; i < n; i++) {
                Job j = jobs[i];
                printf("%d\t%s     \t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", j.job_id, type_name[j.type], j.arrival, j.burst, j.priority, j.start, j.finish, j.wait_time, j.turnaround, j.response_time);
		avgWaits[j.type] += (double)j.wait_time;
		avgResponse[j.type] += (double)j.response_time;
		avgTurnaround[j.type] += (double)j.turnaround;
        }
	for (int i = 0; i < 4; i++) {
		printf("%s: Avg Wait: %.2f, Avg Response: %.2f, Avg Turnaround: %.2f\n", type_name[i], avgWaits[i] / 12.0, avgResponse[i] / 12.0, avgTurnaround[i] / 12.0);
	}
}
