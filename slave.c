#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <string.h>
#include "common.h"

void usage();
void spin(volatile long*, long*, int);
void ebizzy(volatile long*, long*, int);

int main(int argc, char *argv[])
{
	key_t key;
	int   shmid;
	long  *segptr;
	int i, n;

	if(argc < 2 && argc > 5)
		usage();
	sscanf(argv[3], "%d", &i);
	sscanf(argv[4], "%d", &n);

	/* Create unique key via call to ftok() */
	key = ftok(argv[1], 0);

	/* Open the shared memory segment - create if necessary */
	if((shmid = shmget(key, SEGSIZE, IPC_CREAT|IPC_EXCL|0666)) == -1) 
	{
//		printf("Shared memory segment exists - opening as client\n");

		/* Segment probably already exists - try as a client */
		if((shmid = shmget(key, SEGSIZE, 0)) == -1) 
		{
			perror("shmget");
			exit(1);
		}
	}
	else
	{
		printf("Creating new shared memory segment\n");
	}

	/* Attach (map) the shared memory segment into the current process */
	if((segptr = (long *)shmat(shmid, 0, 0)) == (long *)-1)
	{
		perror("shmat");
		exit(1);
	}

	if (strcmp(argv[2], "spin") == 0)
		spin(&segptr[0], &segptr[i], n);
	else if	(strcmp(argv[2], "ebizzy") == 0)
		ebizzy(&segptr[0], &segptr[i], n);

	return 0;
}

void spin(volatile long *idx, long *p, int n)
{
	while(1) {
		int i = 100000;
		while (--i);
		p[*idx * n]++;
	}
}

void usage(void)
{
	fprintf(stderr, "\nUSAGE:  slave fn wl i n\n");
	fprintf(stderr, "\tfn - file name for ftok\n");
	fprintf(stderr, "\twl - type of workload. Available types: spin, ebizzy\n");
	fprintf(stderr, "\ti - index in a shared memory to use\n");
	fprintf(stderr, "\tn - number of co-workers\n");
	exit(1);
}
