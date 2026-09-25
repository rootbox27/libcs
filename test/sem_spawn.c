/* <semaphore.h> and <spawn.h> */
#include <semaphore.h>
#include <spawn.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

extern char **environ;
static sem_t items, done;
static int produced;

static void *consumer(void *arg)
{
	(void)arg;
	for (int i = 0; i < 1000; i++) {
		sem_wait(&items);
		__atomic_fetch_add(&produced, 1, __ATOMIC_SEQ_CST);
	}
	sem_post(&done);
	return 0;
}

static void semaphores(void)
{
	CHECK(sem_init(&items, 0, 0) == 0 && sem_init(&done, 0, 0) == 0);
	pthread_t t[2];
	for (int i = 0; i < 2; i++)
		pthread_create(&t[i], 0, consumer, 0);
	for (int i = 0; i < 2000; i++)
		sem_post(&items);
	sem_wait(&done);
	sem_wait(&done);
	for (int i = 0; i < 2; i++)
		pthread_join(t[i], 0);
	CHECK(produced == 2000);
	int v;
	CHECK(sem_getvalue(&items, &v) == 0 && v == 0);
	CHECK(sem_trywait(&items) == -1 && errno == EAGAIN);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += 20000000;
	if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
	CHECK(sem_timedwait(&items, &ts) == -1 && errno == ETIMEDOUT);
	ts.tv_nsec = 2000000000;
	CHECK(sem_timedwait(&items, &ts) == -1 && errno == EINVAL);
	CHECK(sem_init(&items, 0, (unsigned)SEM_VALUE_MAX + 1) == -1 && errno == EINVAL);

	/* process-shared, across fork */
	sem_t *ps = mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	CHECK(sem_init(ps, 1, 0) == 0);
	pid_t pid = fork();
	if (pid == 0) {
		usleep(10000);
		sem_post(ps);
		_exit(0);
	}
	CHECK(sem_wait(ps) == 0);
	waitpid(pid, 0, 0);

	/* named */
	char name[64];
	snprintf(name, sizeof name, "/citadel-test-%d", (int)getpid());
	sem_t *a = sem_open(name, O_CREAT | O_EXCL, 0600, 3);
	if (a == SEM_FAILED && errno == ENOENT) {
		t_puts("no /dev/shm: skipping named semaphores\n");
		return;
	}
	CHECK(a != SEM_FAILED);
	CHECK(sem_open(name, O_CREAT | O_EXCL, 0600, 3) == SEM_FAILED && errno == EEXIST);
	sem_t *b = sem_open(name, 0);
	CHECK(b == a);
	CHECK(sem_getvalue(b, &v) == 0 && v == 3);
	CHECK(sem_close(b) == 0 && sem_close(a) == 0);
	CHECK(sem_unlink(name) == 0);
	CHECK(sem_open(name, 0) == SEM_FAILED && errno == ENOENT);
	CHECK(sem_open("/a/b", O_CREAT, 0600, 1) == SEM_FAILED && errno == EINVAL);
}

static int run(const char *path, int p, posix_spawn_file_actions_t *fa, posix_spawnattr_t *at, char **argv)
{
	pid_t pid;
	int r = p ? posix_spawnp(&pid, path, fa, at, argv, environ) : posix_spawn(&pid, path, fa, at, argv, environ);
	if (r)
		return -r;
	int st;
	CHECK(waitpid(pid, &st, 0) == pid);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1000 + WTERMSIG(st);
}

static void spawning(void)
{
	char *a1[] = { "sh", "-c", "exit 3", 0 };
	CHECK(run("/bin/sh", 0, 0, 0, a1) == 3);
	CHECK(run("sh", 1, 0, 0, a1) == 3);
	CHECK(run("/nonexistent/prog", 0, 0, 0, a1) == -ENOENT);
	CHECK(run("no-such-program-anywhere", 1, 0, 0, a1) == -ENOENT);

	/* file actions: stdout to a file, via open and dup2 */
	char tmp[] = "/tmp/citadel-spawn-XXXXXX";
	int fd = mkstemp(tmp);
	CHECK(fd >= 0);
	close(fd);
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 5, tmp, O_WRONLY | O_TRUNC, 0);
	posix_spawn_file_actions_adddup2(&fa, 5, 1);
	posix_spawn_file_actions_addclose(&fa, 5);
	char *a2[] = { "sh", "-c", "echo hello; [ -e /proc/self/fd/5 ] && exit 9; exit 0", 0 };
	CHECK(run("/bin/sh", 0, &fa, 0, a2) == 0);
	posix_spawn_file_actions_destroy(&fa);
	char buf[32] = { 0 };
	fd = open(tmp, O_RDONLY);
	CHECK(read(fd, buf, sizeof buf) == 6 && !strcmp(buf, "hello\n"));
	close(fd);
	unlink(tmp);

	/* failing file action is reported */
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 3, "/nonexistent/x", O_RDONLY, 0);
	CHECK(run("/bin/sh", 0, &fa, 0, a1) == -ENOENT);
	posix_spawn_file_actions_destroy(&fa);

	/* attributes: signal mask and defaults, process group */
	signal(SIGUSR1, SIG_IGN);
	posix_spawnattr_t at;
	posix_spawnattr_init(&at);
	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, SIGUSR1);
	posix_spawnattr_setsigdefault(&at, &s);
	posix_spawnattr_setpgroup(&at, 0);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);
	char *a3[] = { "sh", "-c", "kill -USR1 $$; exit 0", 0 };
	CHECK(run("/bin/sh", 0, 0, &at, a3) == 1000 + SIGUSR1);
	short fl;
	CHECK(posix_spawnattr_getflags(&at, &fl) == 0 && fl == (POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP));
	posix_spawnattr_destroy(&at);
	/* without SETSIGDEF an ignored signal stays ignored */
	CHECK(run("/bin/sh", 0, 0, 0, a3) == 0);
	signal(SIGUSR1, SIG_DFL);
}

int main(void)
{
	semaphores();
	spawning();
	return t_done();
}
