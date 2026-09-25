#include "harness.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t got, got_code, got_val;
static void on_usr1(int s) { got = s; }
static void on_info(int s, siginfo_t *si, void *ctx)
{
	got = s;
	got_code = si->si_code;
	got_val = si->si_value.sival_int;
}

static void signals(void)
{
	CHECK(signal(SIGUSR1, on_usr1) == SIG_DFL);
	CHECK(raise(SIGUSR1) == 0 && got == SIGUSR1);
	CHECK(signal(SIGUSR1, SIG_DFL) == on_usr1);

	struct sigaction sa, old;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_info;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	CHECK(sigaction(SIGUSR2, &sa, 0) == 0);
	CHECK(sigaction(SIGUSR2, 0, &old) == 0 && (old.sa_flags & SA_SIGINFO) && old.sa_sigaction == on_info);
	got = 0;
	union sigval v = { .sival_int = 1234 };
	CHECK(sigqueue(getpid(), SIGUSR2, v) == 0);
	CHECK(got == SIGUSR2 && got_code == SI_QUEUE && got_val == 1234);

	/* reserved and invalid signals */
	errno = 0;
	CHECK(sigaction(32, &sa, 0) == -1 && errno == EINVAL);
	CHECK(sigaction(SIGKILL, &sa, 0) == -1);
	CHECK(sigaction(0, &sa, 0) == -1 && sigaction(65, &sa, 0) == -1);
	CHECK(SIGRTMIN == 34 && SIGRTMAX == 64);

	sigset_t s, o, p;
	CHECK(sigemptyset(&s) == 0 && sigismember(&s, SIGINT) == 0);
	CHECK(sigaddset(&s, SIGINT) == 0 && sigismember(&s, SIGINT) == 1);
	CHECK(sigaddset(&s, 33) == -1 && sigaddset(&s, 99) == -1);
	CHECK(sigdelset(&s, SIGINT) == 0 && !sigismember(&s, SIGINT));
	sigfillset(&s);
	CHECK(sigismember(&s, SIGTERM) && !sigismember(&s, 32) && sigismember(&s, SIGRTMIN));

	/* blocked signal stays pending until unblocked */
	CHECK(signal(SIGUSR1, on_usr1) != SIG_ERR);
	sigemptyset(&s);
	sigaddset(&s, SIGUSR1);
	CHECK(sigprocmask(SIG_BLOCK, &s, &o) == 0);
	got = 0;
	kill(getpid(), SIGUSR1);
	CHECK(got == 0);
	CHECK(sigpending(&p) == 0 && sigismember(&p, SIGUSR1));
	CHECK(sigprocmask(SIG_SETMASK, &o, 0) == 0 && got == SIGUSR1);
	CHECK(sigprocmask(99, &s, 0) == -1 && errno == EINVAL);

	/* sigwait consumes a blocked signal without running the handler */
	sigprocmask(SIG_BLOCK, &s, &o);
	got = 0;
	raise(SIGUSR1);
	int sig = 0;
	CHECK(sigwait(&s, &sig) == 0 && sig == SIGUSR1 && got == 0);
	struct timespec ts = { 0, 1000000 };
	errno = 0;
	CHECK(sigtimedwait(&s, 0, &ts) == -1 && errno == EAGAIN);
	sigprocmask(SIG_SETMASK, &o, 0);

	stack_t st = { .ss_sp = malloc(SIGSTKSZ), .ss_size = SIGSTKSZ, .ss_flags = 0 };
	CHECK(sigaltstack(&st, 0) == 0);
	stack_t small = { .ss_sp = st.ss_sp, .ss_size = 100 };
	CHECK(sigaltstack(&small, 0) == -1 && errno == ENOMEM);
	CHECK(!strcmp(strsignal(SIGINT), "Interrupt") && !strcmp(sys_siglist[SIGTERM], "Terminated"));
}

/* The handlers stay registered for every later fork; record only the
 * first few calls. */
static int order[6], on;
static void rec(int v)
{
	if (on < 6)
		order[on] = v;
	on++;
}
static void prep1(void) { rec(1); }
static void prep2(void) { rec(2); }
static void par1(void) { rec(3); }
static void par2(void) { rec(4); }
static void chld(void) { rec(5); }

static void processes(void)
{
	int status;
	pid_t pid = fork();
	if (pid == 0)
		_exit(42);
	CHECK(pid > 0);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 42);

	/* child sees its own pid; exit from main path flushes nothing twice */
	pid = fork();
	if (pid == 0)
		_exit(getpid() == getppid() ? 1 : gettid() == getpid() ? 0 : 2);
	CHECK(waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 0);

	pid = fork();
	if (pid == 0) {
		raise(SIGTERM);
		_exit(0);
	}
	CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
	errno = 0;
	CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);

	CHECK(pthread_atfork(prep1, par1, chld) == 0);
	CHECK(pthread_atfork(prep2, par2, 0) == 0);
	pid = fork();
	if (pid == 0)
		_exit(on == 3 && order[0] == 2 && order[1] == 1 && order[2] == 5 ? 0 : 1);
	waitpid(pid, &status, 0);
	CHECK(WEXITSTATUS(status) == 0);
	CHECK(on == 4 && order[0] == 2 && order[1] == 1 && order[2] == 3 && order[3] == 4);

	/* exec family */
	pid = fork();
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", "exit 7", (char *)0);
		_exit(99);
	}
	waitpid(pid, &status, 0);
	CHECK(WEXITSTATUS(status) == 7);
	pid = fork();
	if (pid == 0) {
		char *argv[] = { "sh", "-c", "test \"$CITADEL_TEST\" = 1 && exit 3", 0 };
		execvp("sh", argv); /* PATH search */
		_exit(99);
	}
	waitpid(pid, &status, 0);
	CHECK(WEXITSTATUS(status) == 3);
	pid = fork();
	if (pid == 0) {
		char *envp[] = { "X=5", 0 };
		execle("/bin/sh", "sh", "-c", "exit $X", (char *)0, envp);
		_exit(99);
	}
	waitpid(pid, &status, 0);
	CHECK(WEXITSTATUS(status) == 5);
	errno = 0;
	CHECK(execvp("citadel-no-such-program", (char *[]){ "x", 0 }) == -1 && errno == ENOENT);

	/* a script without #! runs under the shell */
	char script[64];
	snprintf(script, sizeof script, "/tmp/citadel-script-%d", getpid());
	FILE *f = fopen(script, "w");
	fputs("exit 11\n", f);
	fclose(f);
	chmod(script, 0700);
	pid = fork();
	if (pid == 0) {
		execvp(script, (char *[]){ script, 0 });
		_exit(99);
	}
	waitpid(pid, &status, 0);
	CHECK(WEXITSTATUS(status) == 11);
	unlink(script);

	CHECK(system(0) != 0);
	status = system("exit 9");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 9);
	status = system("kill -TERM $$");
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	f = popen("echo hello; echo world", "r");
	char line[32];
	CHECK(f && fgets(line, sizeof line, f) && !strcmp(line, "hello\n"));
	CHECK(fgets(line, sizeof line, f) && !strcmp(line, "world\n"));
	CHECK(pclose(f) == 0);
	f = popen("read x; exit $x", "w");
	CHECK(f && fputs("6\n", f) >= 0);
	status = pclose(f);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 6);
	errno = 0;
	CHECK(popen("true", "rw") == 0 && errno == EINVAL);

	pid_t sid = getsid(0);
	CHECK(sid > 0 && getpgrp() > 0 && getpgid(0) == getpgrp());
	uid_t r, e, sv;
	CHECK(getresuid(&r, &e, &sv) == 0 && r == getuid() && e == geteuid());
}

int main(void)
{
	signals();
	processes();
	return t_done();
}
