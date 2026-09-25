/* set*id applies to every thread (Linux credentials are per thread) */
#include <grp.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "harness.h"

static volatile int go;
static int pipefd[2];

static void *spinner(void *arg)
{
	(void)arg;
	while (!go)
		sched_yield();
	return (void *)(long)(getuid() * 100000L + geteuid());
}

static void *blocked(void *arg)
{
	(void)arg;
	char c;
	/* blocked in a system call while the credentials change */
	if (read(pipefd[0], &c, 1) != 1)
		return (void *)-1L;
	return (void *)(long)(getuid() * 100000L + geteuid());
}

int main(void)
{
	if (getuid() != 0) {
		t_puts("not root: skipping set*id checks\n");
		return t_done();
	}
	CHECK(pipe(pipefd) == 0);
	pthread_t t[6];
	for (int i = 0; i < 5; i++)
		CHECK(pthread_create(&t[i], 0, spinner, 0) == 0);
	CHECK(pthread_create(&t[5], 0, blocked, 0) == 0);
	usleep(20000);

	CHECK(setgroups(0, 0) == 0);
	CHECK(setgid(65534) == 0);
	CHECK(setresuid(65534, 65534, 65534) == 0);
	CHECK(getuid() == 65534 && geteuid() == 65534 && getgid() == 65534);
	go = 1;
	CHECK(write(pipefd[1], "x", 1) == 1);
	for (int i = 0; i < 6; i++) {
		void *r;
		CHECK(pthread_join(t[i], &r) == 0);
		CHECK((long)r == 65534 * 100000L + 65534);
	}
	/* a thread created afterwards has them too */
	go = 1;
	pthread_t late;
	void *r = 0;
	CHECK(pthread_create(&late, 0, spinner, 0) == 0 && pthread_join(late, &r) == 0);
	CHECK((long)r == 65534 * 100000L + 65534);
	/* and the drop cannot be undone from any thread */
	CHECK(setuid(0) == -1);
	return t_done();
}
