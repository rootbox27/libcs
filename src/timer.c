/* POSIX per-process timers. SIGEV_NONE, SIGEV_SIGNAL and SIGEV_THREAD_ID
 * are kernel timers and the timer_t is the kernel's id. SIGEV_THREAD gets
 * a helper thread, which the kernel signals directly (signal 32, as in
 * glibc) and which calls the notification function; its timer_t points
 * at the helper's state. */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "internal.h"

#define SIGTIMER 32

struct kevent {
	union sigval value;
	int signo, notify, tid, pad[11];
};

struct ttimer {
	int kid;
	volatile int tid;
	volatile int deleting;
	void (*fn)(union sigval);
	union sigval value;
};

static int is_kernel(timer_t t) { return (uintptr_t)t <= INT_MAX; }
static int kid(timer_t t) { return is_kernel(t) ? (int)(intptr_t)t : ((struct ttimer *)t)->kid; }

static void *helper(void *arg)
{
	struct ttimer *t = arg;
	unsigned long set = 1UL << (SIGTIMER - 1);
	t->tid = (int)__sys(SYS_gettid);
	__futex_wake(&t->tid, 1);
	for (;;) {
		siginfo_t si;
		long r = __sys(SYS_rt_sigtimedwait, &set, &si, 0, 8);
		if (r < 0)
			continue;
		if (t->deleting)
			break;
		if (si.si_code == SI_TIMER)
			t->fn(t->value);
	}
	free(t);
	return 0;
}

int timer_create(clockid_t clk, struct sigevent *restrict sev, timer_t *restrict res)
{
	struct kevent ke = { { 0 }, SIGALRM, SIGEV_SIGNAL, 0, { 0 } };
	int id;
	if (!sev || sev->sigev_notify != SIGEV_THREAD) {
		struct kevent *kp = 0;
		if (sev) {
			ke.value = sev->sigev_value;
			ke.signo = sev->sigev_signo;
			ke.notify = sev->sigev_notify;
			if (ke.notify == SIGEV_THREAD_ID)
				ke.tid = sev->sigev_notify_thread_id;
			if (ke.notify == SIGEV_SIGNAL && (ke.signo == SIGTIMER || ke.signo == SIGTIMER + 1)) {
				errno = EINVAL;
				return -1;
			}
			kp = &ke;
		}
		if (sys(SYS_timer_create, clk, kp, &id) < 0)
			return -1;
		*res = (timer_t)(intptr_t)id;
		return 0;
	}

	struct ttimer *t = calloc(1, sizeof *t);
	if (!t)
		return -1;
	t->fn = sev->sigev_notify_function;
	t->value = sev->sigev_value;
	pthread_attr_t attr;
	if (sev->sigev_notify_attributes)
		attr = *sev->sigev_notify_attributes;
	else
		pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* the helper starts with every signal blocked, so the timer signal
	 * stays pending for its sigtimedwait */
	unsigned long all = ~0UL, old;
	__sys(SYS_rt_sigprocmask, SIG_BLOCK, &all, &old, 8);
	pthread_t th;
	int e = pthread_create(&th, &attr, helper, t);
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &old, 0, 8);
	if (e) {
		free(t);
		errno = e;
		return -1;
	}
	while (!t->tid)
		__futex_timedwait(&t->tid, 0, CLOCK_MONOTONIC, 0, 1);
	ke.signo = SIGTIMER;
	ke.notify = SIGEV_THREAD_ID;
	ke.tid = t->tid;
	ke.value.sival_ptr = t;
	if (sys(SYS_timer_create, clk, &ke, &id) < 0) {
		int saved = errno;
		t->deleting = 1;
		__sys(SYS_tgkill, __sys(SYS_getpid), t->tid, SIGTIMER);
		errno = saved;
		return -1;
	}
	t->kid = id;
	*res = t;
	return 0;
}

int timer_delete(timer_t t)
{
	int r = (int)sys(SYS_timer_delete, kid(t));
	if (!is_kernel(t)) {
		struct ttimer *tt = t;
		tt->deleting = 1;
		/* wake the helper; it frees its state and exits */
		__sys(SYS_tgkill, __sys(SYS_getpid), tt->tid, SIGTIMER);
	}
	return r;
}

int timer_settime(timer_t t, int flags, const struct itimerspec *restrict nv, struct itimerspec *restrict old)
{
	return (int)sys(SYS_timer_settime, kid(t), flags, nv, old);
}

int timer_gettime(timer_t t, struct itimerspec *cur) { return (int)sys(SYS_timer_gettime, kid(t), cur); }
int timer_getoverrun(timer_t t) { return (int)sys(SYS_timer_getoverrun, kid(t)); }
