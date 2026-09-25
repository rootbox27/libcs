#ifndef _SIGNAL_H
#define _SIGNAL_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_ERR ((void (*)(int))-1)
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPOLL SIGIO
#define SIGPWR 30
#define SIGSYS 31
#define _NSIG 65
#define NSIG _NSIG
#define SIGRTMIN (__libc_current_sigrtmin())
#define SIGRTMAX 64
#define SA_NOCLDSTOP 1
#define SA_NOCLDWAIT 2
#define SA_SIGINFO 4
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SA_RESTORER 0x04000000
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SI_USER 0
#define SI_KERNEL 0x80
#define SI_QUEUE (-1)
#define SI_TIMER (-2)
#define SI_MESGQ (-3)
#define SI_ASYNCIO (-4)
#define SI_SIGIO (-5)
#define SI_TKILL (-6)
#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192
union sigval { int sival_int; void *sival_ptr; };
struct sigevent {
	union sigval sigev_value;
	int sigev_signo;
	int sigev_notify;
	union {
		char __pad[64 - 2 * sizeof(int) - sizeof(union sigval)];
		pid_t sigev_notify_thread_id;
		struct {
			void (*sigev_notify_function)(union sigval);
			pthread_attr_t *sigev_notify_attributes;
		} __sev_thread;
	} __sev;
};
#define sigev_notify_thread_id __sev.sigev_notify_thread_id
#define sigev_notify_function __sev.__sev_thread.sigev_notify_function
#define sigev_notify_attributes __sev.__sev_thread.sigev_notify_attributes
#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2
#define SIGEV_THREAD_ID 4
typedef struct {
	int si_signo, si_errno, si_code;
	union {
		char __pad[128 - 2*sizeof(int) - sizeof(long)];
		struct { pid_t si_pid; uid_t si_uid; union sigval si_sigval; } __rt;
		struct { pid_t si_pid; uid_t si_uid; int si_status; clock_t si_utime, si_stime; } __chld;
		struct { void *si_addr; short si_addr_lsb; } __fault;
		struct { long si_band; int si_fd; } __poll;
		struct { int si_timerid; int si_overrun; union sigval si_sigval; } __timer;
	} __si;
} siginfo_t;
#define si_pid __si.__rt.si_pid
#define si_uid __si.__rt.si_uid
#define si_value __si.__rt.si_sigval
#define si_status __si.__chld.si_status
#define si_utime __si.__chld.si_utime
#define si_stime __si.__chld.si_stime
#define si_addr __si.__fault.si_addr
#define si_band __si.__poll.si_band
#define si_fd __si.__poll.si_fd
#define si_timerid __si.__timer.si_timerid
#define si_overrun __si.__timer.si_overrun
#define si_int si_value.sival_int
#define si_ptr si_value.sival_ptr
struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	} __sa_handler;
	sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
};
#define sa_handler __sa_handler.sa_handler
#define sa_sigaction __sa_handler.sa_sigaction
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
int __libc_current_sigrtmin(void);
sighandler_t signal(int, sighandler_t);
int raise(int);
int kill(pid_t, int);
int killpg(pid_t, int);
int tgkill(pid_t, pid_t, int);
int sigaction(int, const struct sigaction *__restrict, struct sigaction *__restrict);
int sigprocmask(int, const sigset_t *__restrict, sigset_t *__restrict);
int pthread_sigmask(int, const sigset_t *__restrict, sigset_t *__restrict);
int sigpending(sigset_t *);
int sigsuspend(const sigset_t *);
int sigwait(const sigset_t *__restrict, int *__restrict);
int sigwaitinfo(const sigset_t *__restrict, siginfo_t *__restrict);
int sigtimedwait(const sigset_t *__restrict, siginfo_t *__restrict, const struct timespec *__restrict);
int sigqueue(pid_t, int, union sigval);
int sigaltstack(const stack_t *__restrict, stack_t *__restrict);
int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);
int siginterrupt(int, int);
int pthread_kill(pthread_t, int);
extern const char *const sys_siglist[];
__END_DECLS
#include <sys/ucontext.h>
#endif
