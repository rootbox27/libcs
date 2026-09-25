/* POSIX timers */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "harness.h"

_Static_assert(sizeof(struct sigevent) == 64, "sigevent is 64 bytes");

static volatile int hits, thread_hits;
static volatile void *seen;

static void on_sig(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	(void)ctx;
	if (si->si_code == SI_TIMER)
		seen = si->si_value.sival_ptr;
	hits++;
}

static void on_thread(union sigval v)
{
	seen = v.sival_ptr;
	thread_hits++;
}

static void sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000 };
	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

int main(void)
{
	/* signal notification, periodic */
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_sig;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR1, &sa, 0);
	struct sigevent ev;
	memset(&ev, 0, sizeof ev);
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR1;
	ev.sigev_value.sival_ptr = (void *)&hits;
	timer_t t;
	CHECK(timer_create(CLOCK_MONOTONIC, &ev, &t) == 0);
	struct itimerspec its = { { 0, 10000000 }, { 0, 10000000 } }, cur;
	CHECK(timer_settime(t, 0, &its, 0) == 0);
	for (int i = 0; i < 100 && hits < 3; i++)
		sleep_ms(10);
	CHECK(hits >= 3 && seen == &hits);
	CHECK(timer_gettime(t, &cur) == 0 && cur.it_interval.tv_nsec == 10000000);
	CHECK(timer_getoverrun(t) >= 0);
	CHECK(timer_delete(t) == 0);

	/* no notification: just a clock to poll */
	ev.sigev_notify = SIGEV_NONE;
	CHECK(timer_create(CLOCK_MONOTONIC, &ev, &t) == 0);
	its = (struct itimerspec){ { 0, 0 }, { 5, 0 } };
	CHECK(timer_settime(t, 0, &its, 0) == 0);
	CHECK(timer_gettime(t, &cur) == 0 && cur.it_value.tv_sec >= 4 && cur.it_value.tv_sec <= 5);
	CHECK(timer_delete(t) == 0);

	/* thread notification */
	memset(&ev, 0, sizeof ev);
	ev.sigev_notify = SIGEV_THREAD;
	ev.sigev_notify_function = on_thread;
	ev.sigev_value.sival_ptr = (void *)&thread_hits;
	CHECK(timer_create(CLOCK_REALTIME, &ev, &t) == 0);
	its = (struct itimerspec){ { 0, 5000000 }, { 0, 5000000 } };
	CHECK(timer_settime(t, 0, &its, 0) == 0);
	for (int i = 0; i < 100 && thread_hits < 3; i++)
		sleep_ms(10);
	CHECK(thread_hits >= 3 && seen == &thread_hits);
	CHECK(timer_delete(t) == 0);
	int after = thread_hits;
	sleep_ms(30);
	CHECK(thread_hits <= after + 1);

	/* the library's reserved signals cannot be used */
	memset(&ev, 0, sizeof ev);
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = 32;
	CHECK(timer_create(CLOCK_MONOTONIC, &ev, &t) == -1 && errno == EINVAL);
	/* default: SIGALRM */
	CHECK(timer_create(CLOCK_MONOTONIC, 0, &t) == 0 && timer_delete(t) == 0);
	return t_done();
}
