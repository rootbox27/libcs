/* Mutexes, condition variables, reader-writer locks, barriers and
 * spinlocks, all built on private futexes.
 *
 * Every mutex records its owner, so unlocking a mutex that is not held
 * fails with EPERM for all types (not only error-checking ones). */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

static int valid_ts(const struct timespec *ts)
{
	return !ts || (unsigned long)ts->tv_nsec < 1000000000UL;
}

/* ---- mutex: __lock 0 free, 1 locked, 2 locked with waiters ---- */

int pthread_mutexattr_init(pthread_mutexattr_t *a) { a->__type = PTHREAD_MUTEX_DEFAULT; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t)
{
	if ((unsigned)t > PTHREAD_MUTEX_ERRORCHECK)
		return EINVAL;
	a->__type = t;
	return 0;
}
int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict a, int *__restrict t) { *t = a->__type; return 0; }

int pthread_mutex_init(pthread_mutex_t *__restrict m, const pthread_mutexattr_t *__restrict a)
{
	m->__lock = m->__owner = m->__count = 0;
	m->__type = a ? a->__type : PTHREAD_MUTEX_DEFAULT;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	return m->__lock ? EBUSY : 0;
}

static int acquire(pthread_mutex_t *m, const struct timespec *abs)
{
	int c = 0;
	if (__atomic_compare_exchange_n(&m->__lock, &c, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return 0;
	if (!valid_ts(abs))
		return EINVAL;
	/* spin briefly before sleeping */
	for (int i = 0; i < 100 && __atomic_load_n(&m->__lock, __ATOMIC_RELAXED); i++)
		__builtin_ia32_pause();
	while ((c = __atomic_exchange_n(&m->__lock, 2, __ATOMIC_ACQUIRE)) != 0) {
		int r = __futex_timedwait(&m->__lock, 2, CLOCK_REALTIME, abs, 1);
		if (r == -ETIMEDOUT)
			return ETIMEDOUT;
	}
	return 0;
}

static int lock_common(pthread_mutex_t *m, const struct timespec *abs, int try)
{
	int tid = __self()->tid;
	if (m->__type != PTHREAD_MUTEX_NORMAL && m->__owner == tid) {
		if (m->__type == PTHREAD_MUTEX_ERRORCHECK)
			return EDEADLK;
		if (m->__count == INT_MAX)
			return EAGAIN;
		m->__count++;
		return 0;
	}
	if (try) {
		int c = 0;
		if (!__atomic_compare_exchange_n(&m->__lock, &c, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return EBUSY;
	} else {
		int r = acquire(m, abs);
		if (r)
			return r;
	}
	m->__owner = tid;
	m->__count = 1;
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) { return lock_common(m, 0, 0); }
int pthread_mutex_trylock(pthread_mutex_t *m) { return lock_common(m, 0, 1); }
int pthread_mutex_timedlock(pthread_mutex_t *__restrict m, const struct timespec *__restrict abs)
{
	if (!abs || !valid_ts(abs))
		return EINVAL;
	return lock_common(m, abs, 0);
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	if (!m->__lock || (m->__type != PTHREAD_MUTEX_NORMAL && m->__owner != __self()->tid))
		return EPERM;
	if (m->__type == PTHREAD_MUTEX_RECURSIVE && --m->__count)
		return 0;
	m->__owner = 0;
	m->__count = 0;
	if (__atomic_exchange_n(&m->__lock, 0, __ATOMIC_RELEASE) == 2)
		__futex_wake(&m->__lock, 1);
	return 0;
}

/* ---- condition variables: waiters sleep on a sequence number ---- */

int pthread_condattr_init(pthread_condattr_t *a) { a->__clock = CLOCK_REALTIME; return 0; }
int pthread_condattr_destroy(pthread_condattr_t *a) { return 0; }
int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t c)
{
	if (c != CLOCK_REALTIME && c != CLOCK_MONOTONIC)
		return EINVAL;
	a->__clock = c;
	return 0;
}

int pthread_cond_init(pthread_cond_t *__restrict c, const pthread_condattr_t *__restrict a)
{
	c->__seq = 0;
	c->__clock = a ? a->__clock : CLOCK_REALTIME;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *c) { return 0; }

int pthread_cond_timedwait(pthread_cond_t *__restrict c, pthread_mutex_t *__restrict m, const struct timespec *__restrict abs)
{
	if (!valid_ts(abs))
		return EINVAL;
	if (m->__type != PTHREAD_MUTEX_NORMAL && m->__owner != __self()->tid)
		return EPERM;
	__testcancel();
	int seq = __atomic_load_n(&c->__seq, __ATOMIC_ACQUIRE);
	int count = m->__count;
	/* release the mutex completely, even if recursively held */
	m->__count = 1;
	pthread_mutex_unlock(m);
	int r = __futex_timedwait(&c->__seq, seq, c->__clock, abs, 1);
	/* reacquire before acting on cancellation or returning */
	lock_common(m, 0, 0);
	m->__count = count;
	if (r == -EINTR)
		__testcancel();
	return r == -ETIMEDOUT ? ETIMEDOUT : 0;
}

int pthread_cond_wait(pthread_cond_t *__restrict c, pthread_mutex_t *__restrict m)
{
	return pthread_cond_timedwait(c, m, 0);
}

int pthread_cond_signal(pthread_cond_t *c)
{
	__atomic_fetch_add(&c->__seq, 1, __ATOMIC_RELEASE);
	__futex_wake(&c->__seq, 1);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
	__atomic_fetch_add(&c->__seq, 1, __ATOMIC_RELEASE);
	__futex_wake(&c->__seq, INT_MAX);
	return 0;
}

/* ---- rwlock: __lock = readers, or -1 for a writer; waiters sleep on
 * __seq. Readers are preferred, so recursive read locks cannot deadlock. */

int pthread_rwlock_init(pthread_rwlock_t *__restrict l, const pthread_rwlockattr_t *__restrict a)
{
	*l = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *l) { return l->__lock ? EBUSY : 0; }

static int rw_try(pthread_rwlock_t *l, int write)
{
	int s = __atomic_load_n(&l->__lock, __ATOMIC_RELAXED);
	for (;;) {
		if (write ? s != 0 : (s < 0 || s == INT_MAX))
			return s == INT_MAX ? EAGAIN : EBUSY;
		if (__atomic_compare_exchange_n(&l->__lock, &s, write ? -1 : s + 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			if (write)
				l->__writer = __self()->tid;
			return 0;
		}
	}
}

static int rw_lock(pthread_rwlock_t *l, int write)
{
	if (write && l->__writer == __self()->tid && l->__lock < 0)
		return EDEADLK;
	for (;;) {
		int seq = __atomic_load_n(&l->__seq, __ATOMIC_ACQUIRE);
		int r = rw_try(l, write);
		if (r != EBUSY)
			return r;
		__atomic_fetch_add(&l->__wwait, 1, __ATOMIC_SEQ_CST);
		__futex_timedwait(&l->__seq, seq, CLOCK_REALTIME, 0, 1);
		__atomic_fetch_sub(&l->__wwait, 1, __ATOMIC_SEQ_CST);
	}
}

int pthread_rwlock_rdlock(pthread_rwlock_t *l) { return rw_lock(l, 0); }
int pthread_rwlock_wrlock(pthread_rwlock_t *l) { return rw_lock(l, 1); }
int pthread_rwlock_tryrdlock(pthread_rwlock_t *l) { return rw_try(l, 0); }
int pthread_rwlock_trywrlock(pthread_rwlock_t *l) { return rw_try(l, 1); }

int pthread_rwlock_unlock(pthread_rwlock_t *l)
{
	int s = __atomic_load_n(&l->__lock, __ATOMIC_RELAXED);
	if (!s)
		return EPERM;
	if (s < 0) {
		if (l->__writer != __self()->tid)
			return EPERM;
		l->__writer = 0;
		__atomic_store_n(&l->__lock, 0, __ATOMIC_RELEASE);
	} else if (__atomic_sub_fetch(&l->__lock, 1, __ATOMIC_RELEASE) != 0) {
		return 0;
	}
	__atomic_fetch_add(&l->__seq, 1, __ATOMIC_RELEASE);
	if (__atomic_load_n(&l->__wwait, __ATOMIC_SEQ_CST))
		__futex_wake(&l->__seq, INT_MAX);
	return 0;
}

/* ---- barriers ---- */

int pthread_barrier_init(pthread_barrier_t *__restrict b, const pthread_barrierattr_t *__restrict a, unsigned n)
{
	if (!n)
		return EINVAL;
	pthread_mutex_init(&b->__m, 0);
	pthread_cond_init(&b->__c, 0);
	b->__count = 0;
	b->__target = n;
	b->__cycle = 0;
	return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) { return b->__count ? EBUSY : 0; }

int pthread_barrier_wait(pthread_barrier_t *b)
{
	pthread_mutex_lock(&b->__m);
	unsigned cycle = b->__cycle;
	if (++b->__count == b->__target) {
		b->__count = 0;
		b->__cycle++;
		pthread_cond_broadcast(&b->__c);
		pthread_mutex_unlock(&b->__m);
		return PTHREAD_BARRIER_SERIAL_THREAD;
	}
	int old;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old);
	while (cycle == b->__cycle)
		pthread_cond_wait(&b->__c, &b->__m);
	pthread_setcancelstate(old, 0);
	pthread_mutex_unlock(&b->__m);
	return 0;
}

/* ---- spinlocks ---- */

int pthread_spin_init(pthread_spinlock_t *s, int shared) { *s = 0; return 0; }
int pthread_spin_destroy(pthread_spinlock_t *s) { return 0; }
int pthread_spin_lock(pthread_spinlock_t *s)
{
	while (__atomic_exchange_n(s, 1, __ATOMIC_ACQUIRE))
		while (__atomic_load_n(s, __ATOMIC_RELAXED))
			__builtin_ia32_pause();
	return 0;
}
int pthread_spin_trylock(pthread_spinlock_t *s) { return __atomic_exchange_n(s, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0; }
int pthread_spin_unlock(pthread_spinlock_t *s) { __atomic_store_n(s, 0, __ATOMIC_RELEASE); return 0; }
