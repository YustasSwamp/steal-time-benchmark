#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "common.h"

#define IDX_PER_SLAVE 3
void usage();
int fd;
int   shmid;
long  *segptr;
char group[]="group_XXXXXX";

void sigint(int a)
{
	char cmd[64];
	fprintf(stderr, "Exiting...");
	shmdt(segptr);
	shmctl(shmid, IPC_RMID, 0);
	close(fd);
	sprintf(cmd, "rm -f %s", group);
	system(cmd);
	sprintf(cmd, "cgdelete -g cpu:/%s_1", group);
	system(cmd);
	sprintf(cmd, "cgdelete -g cpu:/%s_2", group);
	system(cmd);
	fprintf(stderr, "done\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	key_t key;
	int n, m;
	int i, current, next;
	long n_sum, m_sum;
	char arg1[64], arg2[64];
	char grp1[64], grp2[64];

	if(argc < 2 && argc > 6)
		usage();
	if (strcmp(argv[1], "spin") &&
		strcmp(argv[1], "ebizzy")) {
		usage();
	}
	sscanf(argv[4], "%d", &n);
	sscanf(argv[5], "%d", &m);

	signal(SIGINT, sigint);
	fd = mkstemp(group);
	/* Create unique key via call to ftok() */
	key = ftok(group, 0);

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
	memset(segptr, 0, sizeof(long) * ((n+m) * IDX_PER_SLAVE + 1));
	current = 0;

	/* create slaves */
	sprintf(arg1, "cgcreate -g cpu:/%s_1", group);
	sprintf(arg2, "cgcreate -g cpu:/%s_2", group);
	system(arg1);
	system(arg2);

	sprintf(arg1, "cgset -r cpu.shares=%s %s_1", argv[2], group);
	sprintf(arg2, "cgset -r cpu.shares=%s %s_2", argv[3], group);
	system(arg1);
	system(arg2);

	sprintf(arg2, "%d", n + m);
	sprintf(grp1, "cpu:%s_1", group);
	sprintf(grp2, "cpu:%s_2", group);
	for (i = 0; i < n; i++) {
		sprintf(arg1, "%d", i + 1);
		if (!fork()) 
			if (execlp("cgexec", "cgexec", "-g", grp1, "./slave",
					group, argv[1], arg1, arg2,
					NULL) == -1)
				perror("execlp");
	}

	for (i = 0; i < m; i++) {
		sprintf(arg1, "%d", n + i + 1);
		if (!fork()) 
			if (execlp("cgexec", "cgexec", "-g", grp2, "./slave",
					group, argv[1], arg1, arg2,
					NULL) == -1)
				perror("execlp");
	}


	while (1) {
		next = current + 1;
		if (next == IDX_PER_SLAVE) next = 0;
		/* clean up old data data in the next indexes */
		memset(&segptr[1 + (n + m) * next], 0, sizeof(long) * (n+m));
		segptr[0] = next;
		n_sum = m_sum = 0;
		for (i = 0; i < n; i++) {
			n_sum += segptr[1 + i + (n+m) * current];
		}
		for (i = 0; i < m; i++) {
			m_sum += segptr[1 + n + i + (n+m) * current];
		}
		if (n_sum || m_sum)
			fprintf(stderr, "%ld%% %ld\n",
				m_sum * 100 / (n_sum + m_sum),
				(n_sum + m_sum));
		current = next;
		sleep(1);

	}
	return 0;
}

void usage(void)
{
	fprintf(stderr, "\nUSAGE:  master wl s1 s2 n m\n");
	fprintf(stderr, "\twl - type of workload. Available types: spin, ebizzy\n");
	fprintf(stderr, "\ts1,s2 - weights of a groups in a cpu.shares\n");
	fprintf(stderr, "\tn - number of processes in a first group\n");
	fprintf(stderr, "\tm - number of processes in a second group\n");
	exit(1);
}
