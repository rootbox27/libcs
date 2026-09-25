/* <ucontext.h>, <monetary.h> and <fmtmsg.h> */
#include <ucontext.h>
#include <monetary.h>
#include <fmtmsg.h>
#include <errno.h>
#include <fenv.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "harness.h"

/* the layout SA_SIGINFO handlers and debuggers depend on */
_Static_assert(offsetof(ucontext_t, uc_mcontext) == 40, "mcontext");
_Static_assert(offsetof(ucontext_t, uc_sigmask) == 296, "sigmask");
_Static_assert(offsetof(ucontext_t, __fpregs_mem) == 424, "fpregs");
_Static_assert(sizeof(ucontext_t) == 968, "size");

static ucontext_t main_ctx, co_ctx, done_ctx;
static int trace[16], ntrace;
static long args_sum;

static void coroutine(int a, int b, int c, int d, int e, int f, int g, int h)
{
	args_sum = a + b * 10L + c * 100L + d * 1000L + e * 10000L + f * 100000L + g * 1000000L + h * 10000000L;
	/* check stack alignment for the ABI: a 16-aligned local */
	long double ld = 1.0L;
	volatile long double *p = &ld;
	CHECK(((unsigned long)p & 15) == 0);
	for (int i = 0; i < 3; i++) {
		trace[ntrace++] = 100 + i;
		swapcontext(&co_ctx, &main_ctx);
	}
	CHECK(fegetround() == FE_TONEAREST);
	trace[ntrace++] = 200;
	/* returning resumes uc_link */
}

static void coroutines(void)
{
	static char stack[64 * 1024] __attribute__((aligned(16)));
	CHECK(getcontext(&co_ctx) == 0);
	co_ctx.uc_stack.ss_sp = stack + 1; /* deliberately misaligned */
	co_ctx.uc_stack.ss_size = sizeof stack - 1;
	co_ctx.uc_link = &done_ctx;
	makecontext(&co_ctx, (void (*)(void))coroutine, 8, 1, 2, 3, 4, 5, 6, 7, 8);

	volatile int finished = 0;
	CHECK(getcontext(&done_ctx) == 0);
	if (!finished) {
		finished = 1;
		fesetround(FE_UPWARD); /* each context carries its own rounding mode */
		for (int i = 0; i < 4; i++) {
			trace[ntrace++] = i;
			CHECK(swapcontext(&main_ctx, &co_ctx) == 0);
			CHECK(fegetround() == FE_UPWARD);
		}
		CHECK(0); /* not reached: the coroutine's return lands in done_ctx */
	}
	fesetround(FE_TONEAREST);
	CHECK(args_sum == 87654321);
	static const int want[] = { 0, 100, 1, 101, 2, 102, 3, 200 };
	CHECK(ntrace == 8 && !memcmp(trace, want, sizeof want));
}

static void masks(void)
{
	/* setcontext restores the signal mask saved by getcontext */
	sigset_t s, cur;
	sigemptyset(&s);
	sigaddset(&s, SIGUSR1);
	sigprocmask(SIG_BLOCK, &s, 0);
	ucontext_t c;
	volatile int pass = 0;
	getcontext(&c);
	sigprocmask(SIG_SETMASK, 0, &cur);
	CHECK(sigismember(&cur, SIGUSR1));
	if (pass++ == 0) {
		sigprocmask(SIG_UNBLOCK, &s, 0);
		setcontext(&c);
		CHECK(0);
	}
	CHECK(pass == 2);
	sigprocmask(SIG_UNBLOCK, &s, 0);
}

static volatile long seen_rip;
static void handler(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;
	seen_rip = uc->uc_mcontext.gregs[REG_RIP];
}

static void siginfo_ctx(void)
{
	struct sigaction sa = { 0 };
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR2, &sa, 0);
	raise(SIGUSR2);
	CHECK(seen_rip != 0);
	signal(SIGUSR2, SIG_DFL);
}

static void money(void)
{
	char b[64];
	CHECK(strfmon(b, sizeof b, "%n", 123.45) == 6 && !strcmp(b, "123.45"));
	CHECK(strfmon(b, sizeof b, "%(#5n", -123.45) == 10 && !strcmp(b, "(  123.45)"));
	CHECK(strfmon(b, sizeof b, "%=*#5n", 123.45) == 9 && !strcmp(b, " **123.45"));
	CHECK(strfmon(b, sizeof b, "%-10.0i|", -2.5) == 11 && !strcmp(b, "-2        |"));
	CHECK(strfmon(b, sizeof b, "[%11.4n] %%", 1.0) == 15 && !strcmp(b, "[     1.0000] %"));
	errno = 0;
	CHECK(strfmon(b, 5, "%n", 123.45) == -1 && errno == E2BIG);
	CHECK(strfmon(b, sizeof b, "%q", 1.0) == -1 && errno == EINVAL);
	CHECK(strfmon(b, 0, "", 1.0) == -1);
	CHECK(strfmon(b, sizeof b, "%99999999999999999999n", 1.0) == -1 && errno == E2BIG);
}

static void msgs(void)
{
	int p[2];
	CHECK(pipe(p) == 0);
	int saved = dup(2);
	dup2(p[1], 2);
	int r1 = fmtmsg(MM_PRINT, "UX:cat", MM_ERROR, "invalid syntax", "refer to manual", "UX:cat:001");
	int r2 = fmtmsg(MM_PRINT, "a:b", MM_NOSEV, MM_NULLTXT, "act", MM_NULLTAG);
	int r3 = fmtmsg(MM_PRINT, "nolabelsep", MM_INFO, "t", 0, 0);
	int r4 = fmtmsg(MM_PRINT, "a:b", 9, "t", 0, 0);
	dup2(saved, 2);
	close(saved);
	close(p[1]);
	char b[256];
	ssize_t n = read(p[0], b, sizeof b - 1);
	close(p[0]);
	b[n > 0 ? n : 0] = 0;
	CHECK(r1 == MM_OK && r2 == MM_OK && r3 == MM_NOTOK && r4 == MM_NOTOK);
	CHECK_STR(b, "UX:cat: ERROR: invalid syntax\nTO FIX: refer to manual  UX:cat:001\na:b: TO FIX: act\n");
}

int main(void)
{
	coroutines();
	masks();
	siginfo_ctx();
	money();
	msgs();
	return t_done();
}
