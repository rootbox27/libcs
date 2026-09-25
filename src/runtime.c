/* errno, syscall return convention, futex locks, fatal errors, exit and
 * atexit handling (with mangled function pointers). */
#include "internal.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

int *__errno_location(void)
{
	return &__self()->errno_val;
}

hidden long __syscall_ret(unsigned long r)
{
	if (r > -4096UL) {
		errno = -(long)r;
		return -1;
	}
	return (long)r;
}

/* ---- futex-based lock (0 = free, 1 = held, 2 = held + waiters) ---- */
hidden int __futex_wait(volatile int *addr, int val, const struct timespec *ts)
{
	return (int)__sys(SYS_futex, addr, 0 | 128 /* WAIT|PRIVATE */, val, ts);
}
hidden int __futex_wake(volatile int *addr, int n)
{
	return (int)__sys(SYS_futex, addr, 1 | 128 /* WAKE|PRIVATE */, n);
}
/* Wait while *addr == val, until an absolute time on clock clk (or
 * forever). Returns 0, -ETIMEDOUT, -EINTR or -EAGAIN. */
hidden int __futex_timedwait(volatile int *addr, int val, clockid_t clk, const struct timespec *abs, int priv)
{
	int op = 9 /* FUTEX_WAIT_BITSET */ | (priv ? 128 : 0) | (abs && clk == CLOCK_REALTIME ? 256 : 0);
	long r = __sys(SYS_futex, addr, op, val, abs, 0, 0xffffffff);
	return r == -ETIMEDOUT || r == -EINTR || r == -EAGAIN ? (int)r : 0;
}

/* Cancellation points call this; the real one is in pthread.c. */
static void no_cancel(void) {}
weak_alias(no_cancel, __testcancel);

hidden void __lock(volatile int *l)
{
	int c = __sync_val_compare_and_swap(l, 0, 1);
	if (c == 0)
		return;
	do {
		if (c == 2 || __sync_val_compare_and_swap(l, 1, 2) != 0)
			__futex_wait(l, 2, 0);
	} while ((c = __sync_val_compare_and_swap(l, 0, 2)) != 0);
}
hidden void __unlock(volatile int *l)
{
	if (__sync_fetch_and_sub(l, 1) != 1) {
		__atomic_store_n(l, 0, __ATOMIC_RELEASE);
		__futex_wake(l, 1);
	}
}

/* ---- fatal error reporting: never uses stdio or malloc ---- */
hidden void __write_str(int fd, const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	while (n) {
		long r = __sys(SYS_write, fd, s, n);
		if (r == -EINTR)
			continue;
		if (r <= 0)
			break;
		s += r;
		n -= (size_t)r;
	}
}

hidden void __fatal(const char *msg)
{
	__write_str(2, "*** citadel: ");
	__write_str(2, msg);
	__write_str(2, " ***: terminated\n");
	abort();
}

hidden void __chk_fail(void)
{
	__fatal("buffer overflow detected");
}

void __stack_chk_fail(void)
{
	__fatal("stack smashing detected");
}
hidden void __stack_chk_fail_local(void) __attribute__((__alias__("__stack_chk_fail")));

void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
	char num[16];
	int i = sizeof num - 1;
	num[i] = 0;
	unsigned u = (unsigned)line;
	do num[--i] = '0' + u % 10; while ((u /= 10) && i);
	__write_str(2, __libc.progname ? __libc.progname : "");
	__write_str(2, ": ");
	__write_str(2, file);
	__write_str(2, ":");
	__write_str(2, num + i);
	__write_str(2, ": ");
	__write_str(2, func);
	__write_str(2, ": Assertion `");
	__write_str(2, expr);
	__write_str(2, "' failed.\n");
	abort();
}

void abort(void)
{
	/* Block everything except SIGABRT, raise it, and if a handler
	 * returns, reset to default and try again; finally force exit. */
	unsigned long set = ~(1UL << (SIGABRT - 1));
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &set, 0, 8);
	__sys(SYS_tkill, __sys(SYS_gettid), SIGABRT);
	struct k_sigaction ksa = { SIG_DFL, SA_RESTORER, __restore_rt, { 0, 0 } };
	__sys(SYS_rt_sigaction, SIGABRT, &ksa, 0, 8);
	__sys(SYS_tkill, __sys(SYS_gettid), SIGABRT);
	set = 0;
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &set, 0, 8);
	for (;;) {
		__sys(SYS_exit_group, 127);
		__asm__ volatile("hlt");
	}
}

/* ---- atexit / __cxa_atexit ---- */
struct exit_fn { uintptr_t fn, arg; int cxa; };
struct exit_block {
	struct exit_block *next;
	int n;
	struct exit_fn fns[62];
};
static struct exit_block exit_head;
static struct exit_block *exit_cur;
static volatile int exit_lock;

static int add_exit(uintptr_t fn, uintptr_t arg, int cxa)
{
	LOCK(exit_lock);
	if (!exit_cur)
		exit_cur = &exit_head;
	if (exit_cur->n == 62) {
		void *m = mmap(0, sizeof(struct exit_block), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (m == MAP_FAILED) {
			UNLOCK(exit_lock);
			return -1;
		}
		struct exit_block *b = m;
		b->next = exit_cur;
		exit_cur = b;
	}
	struct exit_fn *f = &exit_cur->fns[exit_cur->n++];
	f->fn = __ptr_mangle(fn);
	f->arg = arg;
	f->cxa = cxa;
	UNLOCK(exit_lock);
	return 0;
}

int __cxa_atexit(void (*fn)(void *), void *arg, void *dso)
{
	(void)dso;
	return add_exit((uintptr_t)fn, (uintptr_t)arg, 1);
}

int atexit(void (*fn)(void))
{
	return add_exit((uintptr_t)fn, 0, 0);
}

void *__dso_handle = &__dso_handle;

static void run_exit_fns(void)
{
	LOCK(exit_lock);
	while (exit_cur) {
		while (exit_cur->n > 0) {
			struct exit_fn f = exit_cur->fns[--exit_cur->n];
			UNLOCK(exit_lock);
			uintptr_t fn = __ptr_demangle(f.fn);
			if (f.cxa)
				((void (*)(void *))fn)((void *)f.arg);
			else
				((void (*)(void))fn)();
			LOCK(exit_lock);
		}
		exit_cur = exit_cur == &exit_head ? 0 : exit_cur->next;
	}
	UNLOCK(exit_lock);
}

static void (*quick_fns[32])(void);
static int quick_n;
static volatile int quick_lock;

int at_quick_exit(void (*fn)(void))
{
	int r = -1;
	LOCK(quick_lock);
	if (quick_n < 32) {
		quick_fns[quick_n++] = (void (*)(void))__ptr_mangle((uintptr_t)fn);
		r = 0;
	}
	UNLOCK(quick_lock);
	return r;
}

extern void (*const __fini_array_start[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __fini_array_end[])(void) __attribute__((__visibility__("hidden"), __weak__));

void _Exit(int code)
{
	for (;;) {
		__sys(SYS_exit_group, code);
		__asm__ volatile("hlt");
	}
}
extern __typeof(_Exit) _exit __attribute__((__weak__, __alias__("_Exit"), __noreturn__));

/* Replaced by the real stdio flush once stdio exists. */
static void dummy(void) {}
weak_alias(dummy, __stdio_exit);

void exit(int code)
{
	static volatile int exiting;
	/* A second concurrent exit() waits forever rather than racing. */
	if (__sync_lock_test_and_set(&exiting, 1))
		for (;;) __sys(SYS_pause);
	run_exit_fns();
	size_t n = __fini_array_end - __fini_array_start;
	while (n)
		__fini_array_start[--n]();
	__stdio_exit();
	_Exit(code);
}

void quick_exit(int code)
{
	while (quick_n > 0)
		((void (*)(void))__ptr_demangle((uintptr_t)quick_fns[--quick_n]))();
	_Exit(code);
}
