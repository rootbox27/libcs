#include "harness.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static __thread int tls_val = 42;
static __thread char tls_buf[64] = "initial";

static void *ret_arg(void *a)
{
	CHECK(tls_val == 42 && !strcmp(tls_buf, "initial"));
	tls_val = (int)(long)a;
	errno = (int)(long)a;
	strcpy(tls_buf, "changed");
	return (void *)((long)a * 2);
}

static void basics(void)
{
	pthread_t t[16];
	for (long i = 0; i < 16; i++)
		CHECK(pthread_create(&t[i], 0, ret_arg, (void *)(i + 1)) == 0);
	for (long i = 0; i < 16; i++) {
		void *r;
		CHECK(pthread_join(t[i], &r) == 0 && r == (void *)((i + 1) * 2));
	}
	/* the main thread's TLS and errno are untouched */
	CHECK(tls_val == 42 && !strcmp(tls_buf, "initial"));
	CHECK(pthread_join(pthread_self(), 0) == EDEADLK);
	CHECK(pthread_equal(pthread_self(), pthread_self()));

	pthread_attr_t a;
	pthread_attr_init(&a);
	size_t s;
	CHECK(pthread_attr_setstacksize(&a, 1 << 16) == 0 && pthread_attr_getstacksize(&a, &s) == 0 && s == 1 << 16);
	CHECK(pthread_attr_setstacksize(&a, 100) == EINVAL);
	pthread_t tt;
	CHECK(pthread_create(&tt, &a, ret_arg, (void *)5) == 0 && pthread_join(tt, 0) == 0);
	CHECK(pthread_setname_np(pthread_self(), "citadel-main") == 0);
	char name[16];
	CHECK(pthread_getname_np(pthread_self(), name, sizeof name) == 0 && !strcmp(name, "citadel-main"));
	CHECK(pthread_setname_np(pthread_self(), "this-name-is-too-long") == ERANGE);
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static long counter;

static void *hammer(void *a)
{
	for (int i = 0; i < 20000; i++) {
		pthread_mutex_lock(&mtx);
		counter++;
		pthread_mutex_unlock(&mtx);
	}
	return 0;
}

static void mutexes(void)
{
	pthread_t t[8];
	for (int i = 0; i < 8; i++)
		pthread_create(&t[i], 0, hammer, 0);
	for (int i = 0; i < 8; i++)
		pthread_join(t[i], 0);
	CHECK(counter == 8 * 20000);

	CHECK(pthread_mutex_unlock(&mtx) == EPERM); /* not locked */
	pthread_mutexattr_t ma;
	pthread_mutexattr_init(&ma);
	pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_t r;
	pthread_mutex_init(&r, &ma);
	CHECK(pthread_mutex_lock(&r) == 0 && pthread_mutex_lock(&r) == 0 && pthread_mutex_trylock(&r) == 0);
	CHECK(pthread_mutex_unlock(&r) == 0 && pthread_mutex_unlock(&r) == 0 && pthread_mutex_destroy(&r) == EBUSY);
	CHECK(pthread_mutex_unlock(&r) == 0 && pthread_mutex_unlock(&r) == EPERM && pthread_mutex_destroy(&r) == 0);
	pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&r, &ma);
	CHECK(pthread_mutex_lock(&r) == 0 && pthread_mutex_lock(&r) == EDEADLK && pthread_mutex_trylock(&r) == EDEADLK);
	pthread_mutex_unlock(&r);
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec += 20000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	pthread_mutex_lock(&mtx);
	CHECK(pthread_mutex_trylock(&mtx) == EBUSY);
	pthread_mutex_unlock(&mtx);
}

/* bounded queue with two condition variables */
static pthread_mutex_t qm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notempty = PTHREAD_COND_INITIALIZER, notfull = PTHREAD_COND_INITIALIZER;
static int q[4], qn, qh;
static long consumed_sum;

static void *producer(void *a)
{
	for (int i = 1; i <= 5000; i++) {
		pthread_mutex_lock(&qm);
		while (qn == 4)
			pthread_cond_wait(&notfull, &qm);
		q[(qh + qn++) % 4] = i;
		pthread_cond_signal(&notempty);
		pthread_mutex_unlock(&qm);
	}
	return 0;
}

static void *consumer(void *a)
{
	for (int i = 0; i < 5000; i++) {
		pthread_mutex_lock(&qm);
		while (!qn)
			pthread_cond_wait(&notempty, &qm);
		consumed_sum += q[qh];
		qh = (qh + 1) % 4;
		qn--;
		pthread_cond_signal(&notfull);
		pthread_mutex_unlock(&qm);
	}
	return 0;
}

static void conds(void)
{
	pthread_t p[2], c[2];
	for (int i = 0; i < 2; i++) {
		pthread_create(&p[i], 0, producer, 0);
		pthread_create(&c[i], 0, consumer, 0);
	}
	for (int i = 0; i < 2; i++) {
		pthread_join(p[i], 0);
		pthread_join(c[i], 0);
	}
	CHECK(consumed_sum == 2L * 5000 * 5001 / 2);

	/* timed waits on both clocks */
	pthread_cond_t cv;
	pthread_condattr_t ca;
	pthread_condattr_init(&ca);
	CHECK(pthread_condattr_setclock(&ca, CLOCK_MONOTONIC) == 0);
	pthread_cond_init(&cv, &ca);
	struct timespec t0, t1, dl;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	dl = t0;
	dl.tv_nsec += 30000000;
	if (dl.tv_nsec >= 1000000000) {
		dl.tv_sec++;
		dl.tv_nsec -= 1000000000;
	}
	pthread_mutex_lock(&qm);
	CHECK(pthread_cond_timedwait(&cv, &qm, &dl) == ETIMEDOUT);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	CHECK((t1.tv_sec - t0.tv_sec) * 1000000000L + t1.tv_nsec - t0.tv_nsec >= 30000000);
	clock_gettime(CLOCK_REALTIME, &dl);
	dl.tv_nsec = 999999999 + 1;
	CHECK(pthread_cond_timedwait(&notempty, &qm, &dl) == EINVAL);
	pthread_mutex_unlock(&qm);
}

static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static int shared_a, shared_b, rw_bad;

static void *rw_worker(void *arg)
{
	for (int i = 0; i < 3000; i++) {
		if (i % 10 == (int)(long)arg) {
			pthread_rwlock_wrlock(&rw);
			shared_a++;
			shared_b++;
			pthread_rwlock_unlock(&rw);
		} else {
			pthread_rwlock_rdlock(&rw);
			if (shared_a != shared_b)
				rw_bad = 1;
			pthread_rwlock_unlock(&rw);
		}
	}
	return 0;
}

static pthread_barrier_t bar;
static int serial_count, phase_bad, phase;

static void *bar_worker(void *a)
{
	for (int round = 0; round < 50; round++) {
		if (phase != round)
			phase_bad = 1;
		int r = pthread_barrier_wait(&bar);
		if (r == PTHREAD_BARRIER_SERIAL_THREAD) {
			__atomic_fetch_add(&serial_count, 1, __ATOMIC_SEQ_CST);
			phase++;
		}
		pthread_barrier_wait(&bar);
	}
	return 0;
}

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int once_runs;
static void once_fn(void)
{
	struct timespec d = { 0, 5000000 };
	nanosleep(&d, 0);
	once_runs++;
}
static void *once_worker(void *a)
{
	pthread_once(&once, once_fn);
	return (void *)(long)once_runs;
}

static pthread_key_t key;
static int dtor_calls;
static void dtor(void *v)
{
	__atomic_fetch_add(&dtor_calls, 1, __ATOMIC_SEQ_CST);
	free(v);
}
static void *key_worker(void *a)
{
	CHECK(pthread_getspecific(key) == 0);
	pthread_setspecific(key, malloc(8));
	return 0;
}

static void others(void)
{
	pthread_t t[6];
	for (long i = 0; i < 6; i++)
		pthread_create(&t[i], 0, rw_worker, (void *)i);
	for (int i = 0; i < 6; i++)
		pthread_join(t[i], 0);
	CHECK(!rw_bad && shared_a == shared_b && shared_a > 0);
	CHECK(pthread_rwlock_unlock(&rw) == EPERM);
	CHECK(pthread_rwlock_rdlock(&rw) == 0 && pthread_rwlock_rdlock(&rw) == 0 && pthread_rwlock_trywrlock(&rw) == EBUSY);
	pthread_rwlock_unlock(&rw);
	pthread_rwlock_unlock(&rw);
	CHECK(pthread_rwlock_wrlock(&rw) == 0 && pthread_rwlock_tryrdlock(&rw) == EBUSY && pthread_rwlock_wrlock(&rw) == EDEADLK);
	pthread_rwlock_unlock(&rw);

	pthread_barrier_init(&bar, 0, 5);
	for (int i = 0; i < 5; i++)
		pthread_create(&t[i], 0, bar_worker, 0);
	for (int i = 0; i < 5; i++)
		pthread_join(t[i], 0);
	CHECK(serial_count == 50 && !phase_bad);

	for (int i = 0; i < 6; i++)
		pthread_create(&t[i], 0, once_worker, 0);
	for (int i = 0; i < 6; i++) {
		void *r;
		pthread_join(t[i], &r);
		CHECK(r == (void *)1L);
	}
	CHECK(once_runs == 1);

	CHECK(pthread_key_create(&key, dtor) == 0);
	for (int i = 0; i < 6; i++)
		pthread_create(&t[i], 0, key_worker, 0);
	for (int i = 0; i < 6; i++)
		pthread_join(t[i], 0);
	CHECK(dtor_calls == 6);
	CHECK(pthread_setspecific(key, (void *)1) == 0 && pthread_getspecific(key) == (void *)1);
	pthread_setspecific(key, 0);
	CHECK(pthread_key_delete(key) == 0 && pthread_setspecific(key, (void *)1) == EINVAL);

	pthread_spinlock_t sl;
	pthread_spin_init(&sl, 0);
	CHECK(pthread_spin_lock(&sl) == 0 && pthread_spin_trylock(&sl) == EBUSY && pthread_spin_unlock(&sl) == 0);
}

static volatile int detached_done;
static void *detached_fn(void *a)
{
	__atomic_fetch_add(&detached_done, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static void *sleeper(void *a)
{
	struct timespec d = { 10, 0 };
	nanosleep(&d, 0); /* cancellation point */
	return (void *)1;
}

static volatile int spin_forever;
static void *async_victim(void *a)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);
	while (!spin_forever)
		;
	return (void *)1;
}

static void *disabled_victim(void *a)
{
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
	struct timespec d = { 0, 50000000 };
	nanosleep(&d, 0);
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
	pthread_testcancel();
	return (void *)1;
}

static volatile sig_atomic_t usr1_tid;
static void on_usr1(int s) { usr1_tid = gettid(); }
static void *wait_usr1(void *a)
{
	*(int *)a = gettid();
	struct timespec d = { 0, 1000000 };
	while (!usr1_tid)
		nanosleep(&d, 0);
	return 0;
}

static void *stdio_worker(void *a)
{
	FILE *f = a;
	for (int i = 0; i < 1000; i++)
		fprintf(f, "%s", "0123456789\n");
	return 0;
}

static void lifecycle(void)
{
	/* detached threads free themselves */
	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&a, 1 << 16);
	for (int i = 0; i < 200; i++) {
		pthread_t t;
		CHECK(pthread_create(&t, &a, detached_fn, 0) == 0);
	}
	pthread_t t;
	pthread_create(&t, 0, detached_fn, 0);
	CHECK(pthread_detach(t) == 0);
	struct timespec d = { 0, 1000000 };
	for (int i = 0; i < 2000 && detached_done < 201; i++)
		nanosleep(&d, 0);
	CHECK(detached_done == 201);

	/* cancellation */
	void *r;
	pthread_create(&t, 0, sleeper, 0);
	nanosleep(&d, 0);
	CHECK(pthread_cancel(t) == 0 && pthread_join(t, &r) == 0 && r == PTHREAD_CANCELED);
	pthread_create(&t, 0, async_victim, 0);
	nanosleep(&d, 0);
	CHECK(pthread_cancel(t) == 0 && pthread_join(t, &r) == 0 && r == PTHREAD_CANCELED);
	pthread_create(&t, 0, disabled_victim, 0);
	nanosleep(&d, 0);
	pthread_cancel(t);
	CHECK(pthread_join(t, &r) == 0 && r == PTHREAD_CANCELED);

	/* pthread_kill targets one thread */
	signal(SIGUSR1, on_usr1);
	int tid = 0;
	pthread_create(&t, 0, wait_usr1, &tid);
	while (!__atomic_load_n(&tid, __ATOMIC_SEQ_CST))
		nanosleep(&d, 0);
	CHECK(pthread_kill(t, SIGUSR1) == 0);
	pthread_join(t, 0);
	CHECK(usr1_tid == tid && tid != gettid());
	CHECK(pthread_kill(pthread_self(), 32) == EINVAL);

	/* stdio from several threads: lines stay intact */
	char *buf;
	size_t len;
	FILE *f = open_memstream(&buf, &len);
	pthread_t w[4];
	for (int i = 0; i < 4; i++)
		pthread_create(&w[i], 0, stdio_worker, f);
	for (int i = 0; i < 4; i++)
		pthread_join(w[i], 0);
	fclose(f);
	int ok = len == 4 * 1000 * 11;
	for (size_t i = 0; ok && i < len; i += 11)
		ok = !memcmp(buf + i, "0123456789\n", 11);
	CHECK(ok);
	free(buf);

	/* fork from a threaded process: the child has one thread */
	pthread_create(&t, 0, sleeper, 0);
	pid_t pid = fork();
	if (pid == 0) {
		pthread_t c;
		void *cr;
		int good = pthread_create(&c, 0, ret_arg, (void *)3) == 0 && pthread_join(c, &cr) == 0 && cr == (void *)6;
		_exit(good ? 0 : 1);
	}
	int st;
	waitpid(pid, &st, 0);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	pthread_cancel(t);
	pthread_join(t, 0);
}

int main(void)
{
	basics();
	mutexes();
	conds();
	others();
	lifecycle();
	return t_done();
}
