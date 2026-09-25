/* C11 <threads.h> as thin wrappers over the pthread functions. */
#include <threads.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "internal.h"

_Static_assert(sizeof(mtx_t) == sizeof(pthread_mutex_t), "mtx_t layout");
_Static_assert(sizeof(cnd_t) == sizeof(pthread_cond_t), "cnd_t layout");
_Static_assert(sizeof(thrd_t) == sizeof(pthread_t), "thrd_t layout");

static int map(int err)
{
	switch (err) {
	case 0: return thrd_success;
	case EBUSY: return thrd_busy;
	case ETIMEDOUT: return thrd_timedout;
	case ENOMEM: case EAGAIN: return thrd_nomem;
	default: return thrd_error;
	}
}

/* A thrd_start_t returns int where a pthread start routine returns void *;
 * the int result is carried in the pointer. */
struct start { thrd_start_t fn; void *arg; };

static void *trampoline(void *p)
{
	struct start s = *(struct start *)p;
	free(p);
	return (void *)(intptr_t)s.fn(s.arg);
}

int thrd_create(thrd_t *thr, thrd_start_t fn, void *arg)
{
	struct start *s = malloc(sizeof *s);
	if (!s)
		return thrd_nomem;
	s->fn = fn;
	s->arg = arg;
	int r = pthread_create((pthread_t *)thr, 0, trampoline, s);
	if (r)
		free(s);
	return map(r);
}

thrd_t thrd_current(void) { return pthread_self(); }
int thrd_detach(thrd_t t) { return map(pthread_detach(t)); }
int thrd_equal(thrd_t a, thrd_t b) { return pthread_equal(a, b); }
void thrd_exit(int res) { pthread_exit((void *)(intptr_t)res); }

int thrd_join(thrd_t t, int *res)
{
	void *r;
	int e = pthread_join(t, &r);
	if (e)
		return thrd_error;
	if (res)
		*res = (int)(intptr_t)r;
	return thrd_success;
}

int thrd_sleep(const struct timespec *dur, struct timespec *rem)
{
	int saved = errno;
	if (nanosleep(dur, rem) == 0)
		return 0;
	int r = errno == EINTR ? -1 : -2;
	errno = saved;
	return r;
}

void thrd_yield(void) { sched_yield(); }

int mtx_init(mtx_t *m, int type)
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	if (type & mtx_recursive)
		pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	return map(pthread_mutex_init((pthread_mutex_t *)m, &a));
}

void mtx_destroy(mtx_t *m) { pthread_mutex_destroy((pthread_mutex_t *)m); }
int mtx_lock(mtx_t *m) { return map(pthread_mutex_lock((pthread_mutex_t *)m)); }
int mtx_trylock(mtx_t *m) { return map(pthread_mutex_trylock((pthread_mutex_t *)m)); }
int mtx_unlock(mtx_t *m) { return map(pthread_mutex_unlock((pthread_mutex_t *)m)); }

int mtx_timedlock(mtx_t *restrict m, const struct timespec *restrict ts)
{
	return map(pthread_mutex_timedlock((pthread_mutex_t *)m, ts));
}

int cnd_init(cnd_t *c) { return map(pthread_cond_init((pthread_cond_t *)c, 0)); }
void cnd_destroy(cnd_t *c) { pthread_cond_destroy((pthread_cond_t *)c); }
int cnd_broadcast(cnd_t *c) { return map(pthread_cond_broadcast((pthread_cond_t *)c)); }
int cnd_signal(cnd_t *c) { return map(pthread_cond_signal((pthread_cond_t *)c)); }

int cnd_wait(cnd_t *c, mtx_t *m)
{
	return map(pthread_cond_wait((pthread_cond_t *)c, (pthread_mutex_t *)m));
}

int cnd_timedwait(cnd_t *restrict c, mtx_t *restrict m, const struct timespec *restrict ts)
{
	return map(pthread_cond_timedwait((pthread_cond_t *)c, (pthread_mutex_t *)m, ts));
}

int tss_create(tss_t *k, tss_dtor_t dtor) { return map(pthread_key_create((pthread_key_t *)k, dtor)); }
void tss_delete(tss_t k) { pthread_key_delete(k); }
void *tss_get(tss_t k) { return pthread_getspecific(k); }
int tss_set(tss_t k, void *v) { return map(pthread_setspecific(k, v)); }

void call_once(once_flag *f, void (*fn)(void)) { pthread_once((pthread_once_t *)f, fn); }
