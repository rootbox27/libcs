/* Glue between OpenBSD's malloc (malloc.c here) and the rest of Citadel:
 * pool locks, fork and thread start hooks, and the allocation functions
 * malloc.c does not define. */
#include "compat.h"
#include <malloc.h>

hidden volatile int __omalloc_locks[_MALLOC_MUTEXES];

/* Called by fork() around the clone, after the atfork prepare handlers
 * (which may allocate) and before the child/parent ones. Holding every
 * pool lock across the clone means the child's heap is not caught
 * half-updated by another thread; the child then starts unlocked. */
hidden void __malloc_atfork(int phase)
{
	if (!__libc.threaded)
		return;
	if (phase < 0) {
		for (int i = 0; i < _MALLOC_MUTEXES; i++)
			__lock(&__omalloc_locks[i]);
	} else if (phase == 0) {
		for (int i = _MALLOC_MUTEXES; i-- > 0;)
			__unlock(&__omalloc_locks[i]);
	} else {
		for (int i = 0; i < _MALLOC_MUTEXES; i++)
			__omalloc_locks[i] = 0;
	}
}

/* Called by pthread_create before the first thread starts: switch malloc
 * to one pool per group of threads, as OpenBSD's rthreads does. */
hidden void __malloc_threads_start(void)
{
	_malloc_init(1);
}

void *reallocarray(void *p, size_t m, size_t n)
{
	size_t t;
	if (__builtin_mul_overflow(m, n, &t)) {
		errno = ENOMEM;
		return 0;
	}
	return realloc(p, t);
}

void *memalign(size_t align, size_t n)
{
	return aligned_alloc(align, n);
}

void *valloc(size_t n)
{
	void *p;
	int e = posix_memalign(&p, PAGE_SZ, n);
	if (e) {
		errno = e;
		return 0;
	}
	return p;
}

void *pvalloc(size_t n)
{
	if (n > SIZE_MAX - PAGE_SZ) {
		errno = ENOMEM;
		return 0;
	}
	return valloc(ROUND_UP(n ? n : 1, PAGE_SZ));
}

int malloc_trim(size_t pad)
{
	return 0;
}
