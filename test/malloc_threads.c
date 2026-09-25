/* The allocator under threads: per-thread pools, frees from other threads,
 * fork while other threads allocate, and malloc_usable_size. */
#include "harness.h"
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define THREADS 8
#define SLOTS 512

static void *volatile handoff[THREADS][SLOTS];
static volatile int stop;
static volatile int bad;

static uint32_t rnd(uint32_t *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

static void *worker(void *arg)
{
	int id = (int)(intptr_t)arg;
	uint32_t seed = 0x9e3779b9u * (uint32_t)(id + 1);
	void *mine[64] = { 0 };
	size_t sizes[64] = { 0 };
	for (int it = 0; it < 20000; it++) {
		int k = rnd(&seed) % 64;
		if (mine[k]) {
			/* the contents survived: nobody else wrote into it */
			unsigned char *c = mine[k];
			for (size_t i = 0; i < sizes[k]; i += 97)
				if (c[i] != (unsigned char)(k + id))
					bad = 1;
			if (rnd(&seed) % 4 == 0) {
				/* hand it to another thread to free */
				int to = rnd(&seed) % THREADS, slot = rnd(&seed) % SLOTS;
				void *old = __atomic_exchange_n(&handoff[to][slot], mine[k], __ATOMIC_ACQ_REL);
				free(old);
			} else if (rnd(&seed) % 2) {
				size_t n = 1 + rnd(&seed) % 20000;
				unsigned char *q = realloc(mine[k], n);
				if (!q) {
					bad = 1;
					continue;
				}
				memset(q, k + id, n);
				mine[k] = q;
				sizes[k] = n;
				continue;
			} else {
				free(mine[k]);
			}
			mine[k] = 0;
		} else {
			size_t n = 1 + (rnd(&seed) % 8 ? rnd(&seed) % 300 : rnd(&seed) % 70000);
			unsigned char *q = malloc(n);
			if (!q || malloc_usable_size(q) != n) {
				bad = 1;
				continue;
			}
			memset(q, k + id, n);
			mine[k] = q;
			sizes[k] = n;
		}
		/* free whatever other threads handed to us */
		int slot = rnd(&seed) % SLOTS;
		free(__atomic_exchange_n(&handoff[id][slot], (void *)0, __ATOMIC_ACQ_REL));
	}
	for (int k = 0; k < 64; k++)
		free(mine[k]);
	return 0;
}

static void *allocator_loop(void *arg)
{
	while (!stop) {
		void *p = malloc(100 + (size_t)(intptr_t)arg);
		free(p);
	}
	return 0;
}

int main(void)
{
	/* malloc_usable_size is exactly the requested size */
	for (size_t n = 1; n < 70000; n = n * 3 + 1) {
		char *p = malloc(n);
		CHECK(p && malloc_usable_size(p) == n);
		memset(p, 1, malloc_usable_size(p)); /* allowed, must not trip a canary */
		free(p);
	}
	CHECK(malloc_usable_size(0) == 0);

	pthread_t t[THREADS];
	for (int i = 0; i < THREADS; i++)
		CHECK(pthread_create(&t[i], 0, worker, (void *)(intptr_t)i) == 0);
	for (int i = 0; i < THREADS; i++)
		pthread_join(t[i], 0);
	for (int i = 0; i < THREADS; i++)
		for (int j = 0; j < SLOTS; j++)
			free(handoff[i][j]);
	CHECK(!bad);

	/* fork while other threads are inside malloc: the child's heap must
	 * be consistent and unlocked */
	pthread_t bg[4];
	for (int i = 0; i < 4; i++)
		CHECK(pthread_create(&bg[i], 0, allocator_loop, (void *)(intptr_t)i) == 0);
	for (int i = 0; i < 50; i++) {
		pid_t pid = fork();
		if (pid == 0) {
			void *ps[100];
			for (int j = 0; j < 100; j++)
				ps[j] = malloc(16 * j + 1);
			for (int j = 0; j < 100; j++)
				free(ps[j]);
			_exit(0);
		}
		int st;
		CHECK(pid > 0 && waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
	}
	stop = 1;
	for (int i = 0; i < 4; i++)
		pthread_join(bg[i], 0);
	return t_done();
}
