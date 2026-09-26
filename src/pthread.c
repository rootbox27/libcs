/* POSIX threads.
 *
 * Each thread is one private mapping: [guard][stack][TLS][TCB]. The TCB
 * gets the process-wide stack canary and pointer guard. A detached
 * thread unmaps its own mapping on exit; before doing so it clears its
 * CLONE_CHILD_CLEARTID address, so the kernel's zeroing at exit can never
 * hit memory that was re-mapped in the meantime.
 *
 * Cancellation is acted on at pthread_testcancel, pthread_join, the
 * condition variable waits and the sleep functions, or at once in
 * asynchronous mode. It is requested with signal 32, which the library
 * reserves; signals not sent from inside the process are ignored.
 *
 * Live threads are kept on a list so that set*id calls can be applied to
 * every thread (Linux credentials are per thread; see __setxid). A thread
 * leaves the list before it blocks signals on its way out. */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define SIGCANCEL 32
#define SIGSETXID 33
#define DEFAULT_STACK (2u << 20)
#define MIN_STACK 16384
#define DTOR_ITERATIONS 4
#define NKEYS 128

enum { JOINABLE = 0, DETACHED = 1, EXITED = 2 };

hidden volatile int __thread_count = 1;

static int clone_flags(void)
{
	return 0x100    /* CLONE_VM */
	     | 0x200    /* CLONE_FS */
	     | 0x400    /* CLONE_FILES */
	     | 0x800    /* CLONE_SIGHAND */
	     | 0x10000  /* CLONE_THREAD */
	     | 0x40000  /* CLONE_SYSVSEM */
	     | 0x80000  /* CLONE_SETTLS */
	     | 0x100000 /* CLONE_PARENT_SETTID */
	     | 0x200000 /* CLONE_CHILD_CLEARTID */
	     | 0x400000 /* CLONE_DETACHED */;
}

static void block_all(unsigned long *old)
{
	unsigned long all = ~0UL;
	__sys(SYS_rt_sigprocmask, SIG_BLOCK, &all, old, 8);
}

static void set_mask(const unsigned long *m)
{
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, m, 0, 8);
}

/* ---- thread-specific data ---- */

static struct {
	uintptr_t dtor;  /* mangled destructor */
	int used;
} keys[NKEYS];
static volatile int key_lock;

int pthread_key_create(pthread_key_t *k, void (*dtor)(void *))
{
	LOCK(key_lock);
	for (unsigned i = 0; i < NKEYS; i++) {
		if (!keys[i].used) {
			keys[i].used = 1;
			keys[i].dtor = dtor ? __ptr_mangle((uintptr_t)dtor) : 0;
			UNLOCK(key_lock);
			*k = i;
			return 0;
		}
	}
	UNLOCK(key_lock);
	return EAGAIN;
}

int pthread_key_delete(pthread_key_t k)
{
	if (k >= NKEYS || !keys[k].used)
		return EINVAL;
	LOCK(key_lock);
	keys[k].used = 0;
	keys[k].dtor = 0;
	UNLOCK(key_lock);
	return 0;
}

void *pthread_getspecific(pthread_key_t k)
{
	return k < NKEYS ? __self()->tsd[k] : 0;
}

int pthread_setspecific(pthread_key_t k, const void *v)
{
	if (k >= NKEYS || !keys[k].used)
		return EINVAL;
	__self()->tsd[k] = (void *)v;
	return 0;
}

hidden void __tsd_run_dtors(void)
{
	struct pthread *self = __self();
	for (int it = 0; it < DTOR_ITERATIONS; it++) {
		int ran = 0;
		for (unsigned i = 0; i < NKEYS; i++) {
			void *v = self->tsd[i];
			if (!v || !keys[i].used || !keys[i].dtor)
				continue;
			self->tsd[i] = 0;
			((void (*)(void *))__ptr_demangle(keys[i].dtor))(v);
			ran = 1;
		}
		if (!ran)
			break;
	}
}

/* ---- creation and exit ---- */

int pthread_attr_init(pthread_attr_t *a)
{
	memset(a, 0, sizeof *a);
	a->__stacksize = DEFAULT_STACK;
	a->__guardsize = PAGE_SZ;
	return 0;
}
int pthread_attr_destroy(pthread_attr_t *a) { return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t s)
{
	if (s < MIN_STACK || s > SIZE_MAX / 4)
		return EINVAL;
	a->__stacksize = s;
	return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *__restrict a, size_t *__restrict s) { *s = a->__stacksize; return 0; }
int pthread_attr_setguardsize(pthread_attr_t *a, size_t g)
{
	if (g > SIZE_MAX / 4)
		return EINVAL;
	a->__guardsize = g;
	return 0;
}
int pthread_attr_getguardsize(const pthread_attr_t *__restrict a, size_t *__restrict g) { *g = a->__guardsize; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int d)
{
	if (d != PTHREAD_CREATE_JOINABLE && d != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	a->__detach = d;
	return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *d) { *d = a->__detach; return 0; }

/* ---- the list of live threads ---- */

static volatile int list_lock;

hidden long __setxid(long nr, long a, long b, long c);
hidden void __thread_list_lock(void);
hidden void __thread_list_unlock(void);
hidden void __thread_list_fork_child(void);

/* call with list_lock held */
static void list_add(struct pthread *p)
{
	struct pthread *m = __self();
	if (!m->next)
		m->next = m->prev = m;
	p->next = m->next;
	p->prev = m;
	m->next->prev = p;
	m->next = p;
}

static void list_del(struct pthread *p)
{
	if (!p->next)
		return;
	p->prev->next = p->next;
	p->next->prev = p->prev;
	p->next = p->prev = 0;
}

hidden void __thread_list_lock(void) { __lock(&list_lock); }
hidden void __thread_list_unlock(void) { __unlock(&list_lock); }

/* in a fork child: we are the only thread */
hidden void __thread_list_fork_child(void)
{
	struct pthread *self = __self();
	self->next = self->prev = 0;
	list_lock = 0;
}

/* ---- set*id across all threads ---- */

static struct {
	long nr, a, b, c;
	volatile int done;
	volatile int failed;
} xid;

static void setxid_handler(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	(void)ctx;
	if (si->si_code != SI_TKILL || si->si_pid != (pid_t)__sys(SYS_getpid))
		return;
	if (__sys(xid.nr, xid.a, xid.b, xid.c) < 0)
		xid.failed = 1;
	__atomic_fetch_add(&xid.done, 1, __ATOMIC_SEQ_CST);
	__futex_wake(&xid.done, 1);
}

/* Run a credential-changing system call in every thread. It is made in
 * the calling thread first; if that fails nothing else happens. If it
 * then failed in another thread, credentials would differ between
 * threads, which is never safe to continue with. */
hidden long __setxid(long nr, long a, long b, long c)
{
	if (!__libc.threaded)
		return __sys(nr, a, b, c);
	static volatile int installed;
	__lock(&list_lock);
	if (!installed) {
		struct k_sigaction ksa = { (void (*)(int))(void (*)(void))setxid_handler, SA_SIGINFO | SA_RESTORER | SA_RESTART,
		                           __restore_rt, { ~0u, ~0u } };
		__sys(SYS_rt_sigaction, SIGSETXID, &ksa, 0, 8);
		installed = 1;
	}
	long r = __sys(nr, a, b, c);
	if (r < 0) {
		__unlock(&list_lock);
		return r;
	}
	struct pthread *self = __self();
	xid.nr = nr;
	xid.a = a;
	xid.b = b;
	xid.c = c;
	xid.failed = 0;
	xid.done = 0;
	int n = 0;
	pid_t pid = (pid_t)__sys(SYS_getpid);
	for (struct pthread *p = self->next; p && p != self; p = p->next) {
		if (__sys(SYS_tgkill, pid, p->tid, SIGSETXID) == 0)
			n++;
	}
	int d;
	while ((d = xid.done) < n)
		__futex_timedwait(&xid.done, d, CLOCK_MONOTONIC, 0, 1);
	if (xid.failed)
		__fatal("set*id failed in another thread: credentials would be inconsistent");
	__unlock(&list_lock);
	return 0;
}

static int start(void *arg)
{
	struct pthread *self = arg;
	set_mask(&self->sigmask_saved);
	pthread_exit(self->start(self->arg));
}

int pthread_create(pthread_t *__restrict res, const pthread_attr_t *__restrict attr, void *(*fn)(void *), void *__restrict arg)
{
	pthread_attr_t def;
	if (!attr) {
		pthread_attr_init(&def);
		attr = &def;
	}
	size_t stack = ROUND_UP(attr->__stacksize ? attr->__stacksize : DEFAULT_STACK, PAGE_SZ);
	size_t guard = ROUND_UP(attr->__guardsize, PAGE_SZ);
	size_t align = __libc.tls_align;
	size_t tls = __libc.tls_offset;
	size_t top = ROUND_UP(tls + sizeof(struct pthread) + align, PAGE_SZ);
	size_t size = guard + stack + top;

	unsigned char *map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_NORESERVE, -1, 0);
	if (map == MAP_FAILED)
		return EAGAIN;
	if (guard && mprotect(map, guard, PROT_NONE)) {
		munmap(map, size);
		return EAGAIN;
	}
	/* TCB at the top (aligned), TLS block just below it */
	uintptr_t tp = ROUND_DOWN((uintptr_t)(map + size - sizeof(struct pthread)), align);
	struct pthread *p = (struct pthread *)tp;
	memcpy((void *)(tp - tls), __libc.tls_image, __libc.tls_file_size);
	p->self = p;
	p->canary = __libc.canary;
	p->ptr_guard = __libc.ptr_guard;
	p->map_base = map;
	p->map_size = size;
	p->start = fn;
	p->arg = arg;
	p->detached = attr->__detach ? DETACHED : JOINABLE;
	p->tsd = p->tsd_storage;
	p->exit_futex = 1; /* cleared by the kernel when the thread exits */

	/* the stack ends below the TLS block, 16-byte aligned */
	void *sp = (void *)ROUND_DOWN(tp - tls, 16);

	unsigned long old;
	/* before the first thread exists: give malloc its per-thread pools */
	if (!__libc.threaded)
		__malloc_threads_start();
	__libc.threaded = 1;
	__lock(&list_lock);
	block_all(&old);
	p->sigmask_saved = old;
	__atomic_fetch_add(&__thread_count, 1, __ATOMIC_SEQ_CST);
	/* on the list before it runs: a set*id broadcast cannot miss it */
	list_add(p);
	int r = __clone(start, sp, clone_flags(), p, &p->tid, p, (int *)&p->exit_futex);
	if (r < 0)
		list_del(p);
	set_mask(&old);
	__unlock(&list_lock);
	if (r < 0) {
		__atomic_fetch_sub(&__thread_count, 1, __ATOMIC_SEQ_CST);
		munmap(map, size);
		return EAGAIN;
	}
	*res = (pthread_t)p;
	return 0;
}

void pthread_exit(void *result)
{
	struct pthread *self = __self();
	self->result = result;
	self->cancel_disabled = 1;
	if (__run_tls_dtors)
		__run_tls_dtors();
	__tsd_run_dtors();

	/* leave the list while signals are still deliverable */
	__lock(&list_lock);
	list_del(self);
	__unlock(&list_lock);

	unsigned long old;
	block_all(&old);
	/* the last thread to exit ends the process with status 0 */
	if (__atomic_sub_fetch(&__thread_count, 1, __ATOMIC_SEQ_CST) == 0) {
		set_mask(&old);
		exit(0);
	}
	int state = JOINABLE;
	if (!__atomic_compare_exchange_n(&self->detached, &state, EXITED, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) &&
	    state == DETACHED && self->map_base && self->start) {
		/* detached: nobody will join, so release our own mapping.
		 * Clear the exit-time tid store first: the memory is about to
		 * be unmapped and could be reused before the kernel writes. */
		__sys(SYS_set_tid_address, 0);
		__unmapself(self->map_base, self->map_size);
	}
	for (;;)
		__sys(SYS_exit, 0);
}

int pthread_join(pthread_t t, void **res)
{
	struct pthread *p = (struct pthread *)t;
	if (p == __self())
		return EDEADLK;
	if (p->detached == DETACHED)
		return EINVAL;
	int tid;
	while ((tid = p->exit_futex)) {
		__testcancel();
		/* the kernel's CLEARTID wake-up is a shared futex */
		__futex_timedwait(&p->exit_futex, tid, CLOCK_REALTIME, 0, 0);
	}
	if (res)
		*res = p->result;
	if (p->start) /* not the main thread */
		munmap(p->map_base, p->map_size);
	return 0;
}

int pthread_detach(pthread_t t)
{
	struct pthread *p = (struct pthread *)t;
	int state = JOINABLE;
	if (__atomic_compare_exchange_n(&p->detached, &state, DETACHED, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return 0;
	if (state == EXITED)
		return pthread_join(t, 0); /* already finished: reclaim now */
	return EINVAL;
}

pthread_t pthread_self(void) { return (pthread_t)__self(); }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
int pthread_yield(void) { return sched_yield(); }

int pthread_kill(pthread_t t, int sig)
{
	struct pthread *p = (struct pthread *)t;
	if (sig == SIGCANCEL || sig == SIGCANCEL + 1 || sig < 0 || sig >= _NSIG)
		return EINVAL;
	if (!p->exit_futex)
		return ESRCH;
	if (!sig)
		return 0;
	return (int)-__sys(SYS_tgkill, __sys(SYS_getpid), p->tid, sig);
}

int pthread_setname_np(pthread_t t, const char *name)
{
	struct pthread *p = (struct pthread *)t;
	size_t l = strlen(name);
	if (l > 15)
		return ERANGE;
	if (p == __self())
		return prctl(PR_SET_NAME, name, 0, 0, 0) ? errno : 0;
	char path[48];
	snprintf(path, sizeof path, "/proc/self/task/%d/comm", p->tid);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return errno;
	int r = write(fd, name, l) == (ssize_t)l ? 0 : errno;
	close(fd);
	return r;
}

int pthread_getname_np(pthread_t t, char *buf, size_t len)
{
	struct pthread *p = (struct pthread *)t;
	char tmp[16];
	if (p == __self()) {
		if (prctl(PR_GET_NAME, tmp, 0, 0, 0))
			return errno;
	} else {
		char path[48];
		snprintf(path, sizeof path, "/proc/self/task/%d/comm", p->tid);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return errno;
		ssize_t n = read(fd, tmp, sizeof tmp - 1);
		close(fd);
		if (n < 0)
			return errno;
		tmp[n] = 0;
		tmp[strcspn(tmp, "\n")] = 0;
	}
	tmp[15] = 0;
	if (strlen(tmp) >= len)
		return ERANGE;
	strcpy(buf, tmp);
	return 0;
}

/* ---- cancellation ---- */

hidden void __testcancel(void)
{
	struct pthread *self = __self();
	if (self->canceled && !self->cancel_disabled)
		pthread_exit(PTHREAD_CANCELED);
}

void pthread_testcancel(void)
{
	__testcancel();
}

static void cancel_handler(int sig, siginfo_t *si, void *ctx)
{
	struct pthread *self = __self();
	/* only requests from this process count */
	if (si->si_code != SI_TKILL || si->si_pid != (pid_t)__sys(SYS_getpid))
		return;
	if (self->canceled && !self->cancel_disabled && self->cancel_async)
		pthread_exit(PTHREAD_CANCELED);
}

int pthread_cancel(pthread_t t)
{
	static volatile int installed;
	struct pthread *p = (struct pthread *)t;
	if (!installed) {
		struct k_sigaction ksa = { (void (*)(int))(void (*)(void))cancel_handler, SA_SIGINFO | SA_RESTORER, __restore_rt, { ~0u, ~0u } };
		__sys(SYS_rt_sigaction, SIGCANCEL, &ksa, 0, 8);
		installed = 1;
	}
	p->canceled = 1;
	if (p == __self()) {
		if (!p->cancel_disabled && p->cancel_async)
			pthread_exit(PTHREAD_CANCELED);
		return 0;
	}
	/* interrupt blocking calls so a cancellation point is reached */
	if (!p->cancel_disabled && p->exit_futex)
		__sys(SYS_tgkill, __sys(SYS_getpid), p->tid, SIGCANCEL);
	return 0;
}

int pthread_setcancelstate(int st, int *old)
{
	if (st != PTHREAD_CANCEL_ENABLE && st != PTHREAD_CANCEL_DISABLE)
		return EINVAL;
	struct pthread *self = __self();
	if (old)
		*old = self->cancel_disabled;
	self->cancel_disabled = st;
	return 0;
}

int pthread_setcanceltype(int ty, int *old)
{
	if (ty != PTHREAD_CANCEL_DEFERRED && ty != PTHREAD_CANCEL_ASYNCHRONOUS)
		return EINVAL;
	struct pthread *self = __self();
	if (old)
		*old = self->cancel_async;
	self->cancel_async = ty;
	if (ty == PTHREAD_CANCEL_ASYNCHRONOUS)
		__testcancel();
	return 0;
}

/* ---- once ---- */

int pthread_once(pthread_once_t *o, void (*fn)(void))
{
	/* 0: not run, 1: running, 2: done, 3: running with waiters */
	if (__atomic_load_n(o, __ATOMIC_ACQUIRE) == 2)
		return 0;
	int s = 0;
	if (__atomic_compare_exchange_n(o, &s, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
		fn();
		if (__atomic_exchange_n(o, 2, __ATOMIC_RELEASE) == 3)
			__futex_wake(o, INT_MAX);
		return 0;
	}
	while ((s = __atomic_load_n(o, __ATOMIC_ACQUIRE)) != 2) {
		if (s == 1)
			__atomic_compare_exchange_n(o, &s, 3, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
		if (s != 2)
			__futex_wait(o, 3, 0);
	}
	return 0;
}
