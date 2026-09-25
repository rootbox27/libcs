/* C11 <threads.h> */
#include <threads.h>
#include <stdint.h>
#include "harness.h"

static mtx_t m;
static cnd_t c;
static int counter, ready;
static once_flag once = ONCE_FLAG_INIT;
static int once_calls;
static tss_t key;
static int dtor_calls;
static thread_local int tl = 7;

static void init_once(void) { once_calls++; }
static void dtor(void *p) { (void)p; __atomic_fetch_add(&dtor_calls, 1, __ATOMIC_SEQ_CST); }

static int worker(void *arg)
{
	call_once(&once, init_once);
	tss_set(key, arg);
	tl += (int)(intptr_t)arg;
	for (int i = 0; i < 1000; i++) {
		mtx_lock(&m);
		counter++;
		mtx_unlock(&m);
	}
	return (int)(intptr_t)arg + (tss_get(key) == arg) * 100 + (tl == 7 + (int)(intptr_t)arg) * 1000;
}

static int waiter(void *arg)
{
	(void)arg;
	mtx_lock(&m);
	while (!ready)
		cnd_wait(&c, &m);
	mtx_unlock(&m);
	return 5;
}

int main(void)
{
	CHECK(mtx_init(&m, mtx_plain) == thrd_success);
	CHECK(cnd_init(&c) == thrd_success);
	CHECK(tss_create(&key, dtor) == thrd_success);
	thrd_t t[4];
	for (int i = 0; i < 4; i++)
		CHECK(thrd_create(&t[i], worker, (void *)(intptr_t)(i + 1)) == thrd_success);
	for (int i = 0; i < 4; i++) {
		int r = -1;
		CHECK(thrd_join(t[i], &r) == thrd_success);
		CHECK(r == i + 1 + 100 + 1000);
	}
	CHECK(counter == 4000 && once_calls == 1 && dtor_calls == 4 && tl == 7);

	thrd_t w;
	CHECK(thrd_create(&w, waiter, 0) == thrd_success);
	struct timespec d = { 0, 10000000 };
	CHECK(thrd_sleep(&d, 0) == 0);
	mtx_lock(&m);
	ready = 1;
	cnd_broadcast(&c);
	mtx_unlock(&m);
	int r;
	CHECK(thrd_join(w, &r) == thrd_success && r == 5);

	/* timed and try operations */
	mtx_t tm;
	CHECK(mtx_init(&tm, mtx_timed) == thrd_success);
	CHECK(mtx_lock(&tm) == thrd_success);
	CHECK(mtx_trylock(&tm) == thrd_busy);
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	ts.tv_nsec += 20000000;
	if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
	CHECK(cnd_timedwait(&c, &tm, &ts) == thrd_timedout);
	mtx_unlock(&tm);
	mtx_t rm;
	CHECK(mtx_init(&rm, mtx_recursive) == thrd_success);
	CHECK(mtx_lock(&rm) == thrd_success && mtx_lock(&rm) == thrd_success);
	mtx_unlock(&rm);
	mtx_unlock(&rm);
	CHECK(thrd_equal(thrd_current(), thrd_current()));
	thrd_yield();
	mtx_destroy(&m);
	cnd_destroy(&c);
	tss_delete(key);
	return t_done();
}
