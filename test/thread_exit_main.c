/* main may call pthread_exit: the process lives until the last thread
 * ends, then exits with status 0 and flushes stdio. */
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static void *late(void *a)
{
	struct timespec d = { 0, 50000000 };
	nanosleep(&d, 0);
	printf("worker finished\n");
	return 0;
}

int main(void)
{
	pthread_t t;
	pthread_create(&t, 0, late, 0);
	printf("main exiting\n");
	pthread_exit(0);
}
