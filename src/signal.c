/* Signals.
 *
 * Signals 32 and 33 are reserved for the library (thread cancellation and
 * set*id broadcast), as in glibc: SIGRTMIN is 34, handlers cannot be
 * installed for the reserved signals and they cannot be blocked. */
#include "internal.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#define SIGRT_RESERVED_LO 32
#define SIGRT_RESERVED_HI 33
#define KSIGSET 8 /* bytes the kernel uses */

static int reserved(int sig)
{
	return sig >= SIGRT_RESERVED_LO && sig <= SIGRT_RESERVED_HI;
}

static int valid(int sig)
{
	return sig > 0 && sig < _NSIG;
}

int __libc_current_sigrtmin(void)
{
	return SIGRT_RESERVED_HI + 1;
}

/* ---- sets ---- */

int sigemptyset(sigset_t *s)
{
	memset(s, 0, sizeof *s);
	return 0;
}

int sigfillset(sigset_t *s)
{
	memset(s, 0, sizeof *s);
	s->__bits[0] = ~0UL & ~(3UL << (SIGRT_RESERVED_LO - 1));
	return 0;
}

int sigaddset(sigset_t *s, int sig)
{
	if (!valid(sig) || reserved(sig)) {
		errno = EINVAL;
		return -1;
	}
	s->__bits[0] |= 1UL << (sig - 1);
	return 0;
}

int sigdelset(sigset_t *s, int sig)
{
	if (!valid(sig) || reserved(sig)) {
		errno = EINVAL;
		return -1;
	}
	s->__bits[0] &= ~(1UL << (sig - 1));
	return 0;
}

int sigismember(const sigset_t *s, int sig)
{
	if (!valid(sig)) {
		errno = EINVAL;
		return -1;
	}
	return !!(s->__bits[0] & (1UL << (sig - 1)));
}

/* ---- masks ---- */

int pthread_sigmask(int how, const sigset_t *__restrict set, sigset_t *__restrict old)
{
	sigset_t tmp;
	if (set) {
		if ((unsigned)how > SIG_SETMASK)
			return EINVAL;
		tmp = *set;
		tmp.__bits[0] &= ~(3UL << (SIGRT_RESERVED_LO - 1));
		set = &tmp;
	}
	long r = __sys(SYS_rt_sigprocmask, how, set, old, KSIGSET);
	if (!r && old) {
		/* only the kernel's 8 bytes were written */
		memset((char *)old + KSIGSET, 0, sizeof *old - KSIGSET);
	}
	return (int)-r;
}

int sigprocmask(int how, const sigset_t *__restrict set, sigset_t *__restrict old)
{
	int r = pthread_sigmask(how, set, old);
	if (r) {
		errno = r;
		return -1;
	}
	return 0;
}

int sigpending(sigset_t *s)
{
	memset(s, 0, sizeof *s);
	return (int)sys(SYS_rt_sigpending, s, KSIGSET);
}

int sigsuspend(const sigset_t *mask)
{
	return (int)sys(SYS_rt_sigsuspend, mask, KSIGSET);
}

/* ---- handlers ---- */

int sigaction(int sig, const struct sigaction *__restrict sa, struct sigaction *__restrict old)
{
	if (!valid(sig) || reserved(sig) || (sa && (sig == SIGKILL || sig == SIGSTOP))) {
		errno = EINVAL;
		return -1;
	}
	struct k_sigaction ksa, kold;
	if (sa) {
		ksa.handler = sa->sa_handler;
		ksa.flags = (unsigned long)sa->sa_flags | SA_RESTORER;
		ksa.restorer = __restore_rt;
		memcpy(ksa.mask, &sa->sa_mask, KSIGSET);
	}
	long r = __sys(SYS_rt_sigaction, sig, sa ? &ksa : 0, old ? &kold : 0, KSIGSET);
	if (r)
		return (int)__syscall_ret((unsigned long)r);
	if (old) {
		memset(old, 0, sizeof *old);
		old->sa_handler = kold.handler;
		old->sa_flags = (int)(kold.flags & ~(unsigned long)SA_RESTORER);
		memcpy(&old->sa_mask, kold.mask, KSIGSET);
	}
	return 0;
}

sighandler_t signal(int sig, sighandler_t h)
{
	struct sigaction sa, old;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = h;
	sa.sa_flags = SA_RESTART;
	if (sigaction(sig, &sa, &old) < 0)
		return SIG_ERR;
	return old.sa_handler;
}

int siginterrupt(int sig, int flag)
{
	struct sigaction sa;
	if (sigaction(sig, 0, &sa) < 0)
		return -1;
	if (flag)
		sa.sa_flags &= ~SA_RESTART;
	else
		sa.sa_flags |= SA_RESTART;
	return sigaction(sig, &sa, 0);
}

int sigaltstack(const stack_t *__restrict ss, stack_t *__restrict old)
{
	if (ss) {
		if (ss->ss_flags & ~SS_DISABLE) {
			errno = EINVAL;
			return -1;
		}
		if (!(ss->ss_flags & SS_DISABLE) && ss->ss_size < MINSIGSTKSZ) {
			errno = ENOMEM;
			return -1;
		}
	}
	return (int)sys(SYS_sigaltstack, ss, old);
}

/* ---- sending ---- */

int kill(pid_t pid, int sig) { return (int)sys(SYS_kill, pid, sig); }
int killpg(pid_t pg, int sig)
{
	if (pg < 0) {
		errno = EINVAL;
		return -1;
	}
	return kill(-pg, sig);
}
int tgkill(pid_t tgid, pid_t tid, int sig) { return (int)sys(SYS_tgkill, tgid, tid, sig); }

int raise(int sig)
{
	/* Block everything so the signal is delivered to this thread, on
	 * return from the tkill, and no handler runs in between. */
	sigset_t all, old;
	memset(&all, 0xff, sizeof all);
	__sys(SYS_rt_sigprocmask, SIG_BLOCK, &all, &old, KSIGSET);
	int r = (int)sys(SYS_tkill, __self()->tid, sig);
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &old, 0, KSIGSET);
	return r;
}

int sigqueue(pid_t pid, int sig, union sigval v)
{
	siginfo_t si;
	memset(&si, 0, sizeof si);
	si.si_signo = sig;
	si.si_code = SI_QUEUE;
	si.si_value = v;
	si.si_pid = (pid_t)__sys(SYS_getpid);
	si.si_uid = (uid_t)__sys(SYS_getuid);
	return (int)sys(SYS_rt_sigqueueinfo, pid, sig, &si);
}

/* ---- waiting ---- */

int sigtimedwait(const sigset_t *__restrict set, siginfo_t *__restrict si, const struct timespec *__restrict ts)
{
	long r;
	do r = __sys(SYS_rt_sigtimedwait, set, si, ts, KSIGSET);
	while (r == -EINTR);
	return (int)__syscall_ret((unsigned long)r);
}

int sigwaitinfo(const sigset_t *__restrict set, siginfo_t *__restrict si)
{
	return sigtimedwait(set, si, 0);
}

int sigwait(const sigset_t *__restrict set, int *__restrict sig)
{
	siginfo_t si;
	int r = sigtimedwait(set, &si, 0);
	if (r < 0)
		return errno;
	*sig = r;
	return 0;
}

int pause(void)
{
	return (int)sys(SYS_pause);
}
