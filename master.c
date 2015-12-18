#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include "common.h"

#define ANSI_COLOR_RED_BOLD	"\x1b[31;1m"
#define ANSI_COLOR_RED		"\x1b[31m"
#define ANSI_COLOR_YELLOW	"\x1b[33;5m"
#define ANSI_COLOR_RESET	"\x1b[0m"
#define IDX_PER_SLAVE 3
#define GROUPS_MAX 10
void usage();

struct group_info {
	int processes;
	int shares;
	float expected_percentage;

} groups[GROUPS_MAX];
int total_groups = 0;
int fd;
int   shmid;
long  *segptr;
char group_prefix[]="group_XXXXXX";

void sigint(int a)
{
	char cmd[64];
	int i;
	fprintf(stderr, "Exiting...");
	shmdt(segptr);
	shmctl(shmid, IPC_RMID, 0);
	close(fd);
	sprintf(cmd, "rm -f %s", group_prefix);
	system(cmd);
	for (i = 0; i < total_groups; i++) {
		sprintf(cmd, "cgdelete -g cpu:/%s_%d", group_prefix, i);
		system(cmd);
	}
	fprintf(stderr, "done\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	key_t key;
	int i, j, current, next;
	long group_sum[GROUPS_MAX], total_sum;
	char arg1[64], arg2[64];
	char cmd[64], grp[64];
	int total_processes = 0;
	int total_shares = 0;
	int process_idx = 0;

	if(argc < 3 && argc > 13)
		usage();
	if (strcmp(argv[1], "spin") &&
		strcmp(argv[1], "ebizzy")) {
		usage();
	}
	signal(SIGINT, sigint);
	fd = mkstemp(group_prefix);
	/* Create unique key via call to ftok() */
	key = ftok(group_prefix, 0);

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

	total_groups = argc - 2;
	for (i = 0; i < total_groups; i++) {
		sscanf(argv[i + 2], "%d:%d", &groups[i].processes, &groups[i].shares);
		sprintf(cmd, "cgcreate -g cpu:/%s_%d", group_prefix, i);
		system(cmd);
		sprintf(cmd, "cgset -r cpu.shares=%d %s_%d", groups[i].shares, group_prefix, i);
		system(cmd);
		total_processes += groups[i].processes;
		total_shares += groups[i].shares;
	}
	for (i = 0; i < total_groups; i++)
		groups[i].expected_percentage = (float) groups[i].shares * 100 / total_shares;

	memset(segptr, 0, sizeof(long) * (total_processes * IDX_PER_SLAVE + 1));
	current = 0;

	/* create slaves */
	sprintf(arg2, "%d", total_processes);
	for (i = 0; i < total_groups; i++) {
		sprintf(grp, "cpu:%s_%d", group_prefix, i);
		for (j = 0; j < groups[i].processes; j++) {
			sprintf(arg1, "%d", ++process_idx);
			if (!fork())
#if 1
				if (execlp("cgexec", "cgexec", "-g", grp, "./slave",
							group_prefix, argv[1], arg1, arg2,
							NULL) == -1)
#else
				if (execlp("./slave", "./slave",
							group_prefix, argv[1], arg1, arg2,
							NULL) == -1)
#endif
					perror("execlp");
		}
	}

	/* start the benchmark */
	while (1) {
		next = current + 1;
		if (next == IDX_PER_SLAVE) next = 0;
		/* clean up old data data in the next indexes */
		memset(&segptr[1 + total_processes * next], 0, sizeof(long) * total_processes);
		segptr[0] = next;
		memset(group_sum, 0, sizeof(long) * total_groups);
		process_idx = 0;
		total_sum = 0;
		for (i = 0; i < total_groups; i++) {
			for (j = 0; j < groups[i].processes; j++) {
				group_sum[i] +=	segptr[1 + process_idx + (total_processes) * current];
				process_idx++;
			}
			total_sum += group_sum[i];
		}

		for (i = 0; i < total_groups; i++) {
			float percentage = !group_sum[i] ?  0.0 :
				(float)group_sum[i] * 100 / total_sum;
			float delta;
			if (percentage == 0.0)
				fprintf(stderr, ANSI_COLOR_RED_BOLD "  0.0 " ANSI_COLOR_RESET);
			else if (percentage < 0.1)
				fprintf(stderr, "%6.2f", percentage);
			else
				fprintf(stderr, "%5.1f ", percentage);
			delta = percentage - groups[i].expected_percentage;
			if (fabs(delta) >= 10.0)
				fprintf(stderr, "(" ANSI_COLOR_RED_BOLD "%5.1f" ANSI_COLOR_RESET ")  ",
					percentage - groups[i].expected_percentage);
			else if (fabs(delta) > 3.0)
				fprintf(stderr, "(" ANSI_COLOR_RED "%5.1f" ANSI_COLOR_RESET ")  ",
					percentage - groups[i].expected_percentage);
			else if (fabs(delta) > 1.5)
				fprintf(stderr, "(" ANSI_COLOR_YELLOW "%5.1f" ANSI_COLOR_RESET ")  ",
					percentage - groups[i].expected_percentage);
			else
				fprintf(stderr, "(%5.1f)  ", percentage - groups[i].expected_percentage);

		}
		fprintf(stderr, "%8ld\n", total_sum);
		current = next;
		sleep(1);

	}
	return 0;
}

void usage(void)
{
	fprintf(stderr, "\nUSAGE: ./master <wl> <n1:s1> [n2:s2] [n3:s3] ...[n10:s10]\n");
	fprintf(stderr, "\twl - type of workload. Available types: spin, ebizzy\n");
	fprintf(stderr, "\tnX - number of processes in a Xth group\n");
	fprintf(stderr, "\tsX - weight of Xth group in a cpu.shares\n");
	exit(1);
}
