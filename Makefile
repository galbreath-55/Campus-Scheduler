all: Campus_Scheduler

Campus_Scheduler:
	gcc -Wall -g -pthread -o campus_scheduler Campus_Scheduler.c
clean:
	rm -rf *.o campus_scheduler

