/* Processes: fork, pthread_atfork, the exec family, wait family, system,
 * and process identity. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- fork ---- */

struct atfork {
	void (*prepare)(void), (*parent)(void), (*child)(void);
};
static struct atfork *atfork_tab;
static size_t atfork_n, atfork_cap;
static volatile int atfork_lock;

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
	LOCK(atfork_lock);
	if (atfork_n == atfork_cap) {
		size_t cap = atfork_cap ? atfork_cap * 2 : 8;
		struct atfork *t = reallocarray(atfork_tab, cap, sizeof *t);
		if (!t) {
			UNLOCK(atfork_lock);
			return ENOMEM;
		}
		atfork_tab = t;
		atfork_cap = cap;
	}
	atfork_tab[atfork_n++] = (struct atfork){ prepare, parent, child };
	UNLOCK(atfork_lock);
	return 0;
}

pid_t fork(void)
{
	/* prepare handlers run in reverse registration order */
	LOCK(atfork_lock);
	size_t n = atfork_n;
	for (size_t i = n; i-- > 0;)
		if (atfork_tab[i].prepare)
			atfork_tab[i].prepare();

	sigset_t all, old;
	memset(&all, 0xff, sizeof all);
	__sys(SYS_rt_sigprocmask, SIG_BLOCK, &all, &old, 8);
	long r = __sys(SYS_clone, SIGCHLD, 0, 0, 0, 0);
	if (r == 0) {
		struct pthread *self = __self();
		self->tid = (int)__sys(SYS_set_tid_address, &self->exit_futex);
		atfork_lock = 0;
	}
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &old, 0, 8);

	for (size_t i = 0; i < n; i++) {
		void (*fn)(void) = r == 0 ? atfork_tab[i].child : atfork_tab[i].parent;
		if (fn)
			fn();
	}
	if (r != 0)
		UNLOCK(atfork_lock);
	return (pid_t)__syscall_ret((unsigned long)r);
}

/* vfork is fork: sharing the parent's memory is not worth the hazards. */
pid_t vfork(void)
{
	return fork();
}

/* ---- exec ---- */

int execve(const char *path, char *const argv[], char *const envp[])
{
	return (int)sys(SYS_execve, path, argv, envp);
}

int execv(const char *path, char *const argv[])
{
	return execve(path, argv, __environ);
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
	return (int)sys(SYS_execveat, fd, "", argv, envp, AT_EMPTY_PATH);
}

/* POSIX: a file that is not a valid executable is run by the shell. */
static void exec_script(const char *path, char *const argv[], char *const envp[])
{
	size_t argc = 0;
	while (argv[argc])
		argc++;
	/* "sh" path argv[1..argc-1] NULL; argv[0] is replaced by path */
	char *nargv[argc + 3];
	nargv[0] = (char *)"sh";
	nargv[1] = (char *)path;
	size_t k = 2;
	for (size_t i = 1; i < argc; i++)
		nargv[k++] = argv[i];
	nargv[k] = 0;
	execve("/bin/sh", nargv, envp);
}

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	if (!*file) {
		errno = ENOENT;
		return -1;
	}
	if (strchr(file, '/')) {
		execve(file, argv, envp);
		if (errno == ENOEXEC)
			exec_script(file, argv, envp);
		return -1;
	}
	const char *path = getenv("PATH");
	if (!path)
		path = "/bin:/usr/bin";
	size_t fl = strnlen(file, NAME_MAX + 1);
	if (fl > NAME_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	int seen_eacces = 0;
	char buf[PATH_MAX];
	for (const char *p = path;; p++) {
		const char *z = strchrnul(p, ':');
		size_t dl = (size_t)(z - p);
		if (dl + fl + 2 <= sizeof buf) {
			/* an empty entry means the current directory */
			memcpy(buf, p, dl);
			size_t o = dl;
			if (dl)
				buf[o++] = '/';
			memcpy(buf + o, file, fl + 1);
			execve(buf, argv, envp);
			switch (errno) {
			case EACCES:
				seen_eacces = 1;
				/* fall through */
			case ENOENT:
			case ENOTDIR:
			case ELOOP:
			case ENAMETOOLONG:
				break;
			case ENOEXEC:
				exec_script(buf, argv, envp);
				return -1;
			default:
				return -1;
			}
		}
		if (!*z)
			break;
		p = z;
	}
	if (seen_eacces)
		errno = EACCES;
	return -1;
}

int execvp(const char *file, char *const argv[])
{
	return execvpe(file, argv, __environ);
}

/* The execl variants gather their arguments into an array on the stack. */
#define GATHER(first, ap, argv, argc)                  \
	do {                                           \
		va_list c_;                            \
		va_copy(c_, ap);                       \
		for (argc = 1; va_arg(c_, char *); argc++) ; \
		va_end(c_);                            \
	} while (0)

int execl(const char *path, const char *arg0, ...)
{
	va_list ap;
	size_t argc;
	va_start(ap, arg0);
	GATHER(arg0, ap, argv, argc);
	char *argv[argc + 1];
	argv[0] = (char *)arg0;
	for (size_t i = 1; i <= argc; i++)
		argv[i] = va_arg(ap, char *);
	va_end(ap);
	return execv(path, argv);
}

int execlp(const char *file, const char *arg0, ...)
{
	va_list ap;
	size_t argc;
	va_start(ap, arg0);
	GATHER(arg0, ap, argv, argc);
	char *argv[argc + 1];
	argv[0] = (char *)arg0;
	for (size_t i = 1; i <= argc; i++)
		argv[i] = va_arg(ap, char *);
	va_end(ap);
	return execvp(file, argv);
}

int execle(const char *path, const char *arg0, ...)
{
	va_list ap;
	size_t argc;
	va_start(ap, arg0);
	GATHER(arg0, ap, argv, argc);
	char *argv[argc + 1];
	argv[0] = (char *)arg0;
	for (size_t i = 1; i <= argc; i++)
		argv[i] = va_arg(ap, char *);
	char **envp = va_arg(ap, char **);
	va_end(ap);
	return execve(path, argv, envp);
}

/* ---- wait ---- */

pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru)
{
	return (pid_t)sys(SYS_wait4, pid, status, options, ru);
}
pid_t waitpid(pid_t pid, int *status, int options) { return wait4(pid, status, options, 0); }
pid_t wait(int *status) { return wait4(-1, status, 0, 0); }
pid_t wait3(int *status, int options, struct rusage *ru) { return wait4(-1, status, options, ru); }
int waitid(idtype_t type, id_t id, siginfo_t *info, int options)
{
	return (int)sys(SYS_waitid, type, id, info, options, 0);
}

/* ---- system ---- */

int system(const char *cmd)
{
	if (!cmd)
		return access("/bin/sh", X_OK) == 0;

	/* POSIX: ignore SIGINT and SIGQUIT and block SIGCHLD while waiting */
	struct sigaction ign, oint, oquit;
	memset(&ign, 0, sizeof ign);
	ign.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ign, &oint);
	sigaction(SIGQUIT, &ign, &oquit);
	sigset_t chld, omask;
	sigemptyset(&chld);
	sigaddset(&chld, SIGCHLD);
	sigprocmask(SIG_BLOCK, &chld, &omask);

	int status = -1;
	pid_t pid = fork();
	if (pid == 0) {
		sigaction(SIGINT, &oint, 0);
		sigaction(SIGQUIT, &oquit, 0);
		sigprocmask(SIG_SETMASK, &omask, 0);
		char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
		execve("/bin/sh", argv, __environ);
		_exit(127);
	}
	if (pid > 0) {
		while (waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR) {
				status = -1;
				break;
			}
		}
	}
	int e = errno;
	sigaction(SIGINT, &oint, 0);
	sigaction(SIGQUIT, &oquit, 0);
	sigprocmask(SIG_SETMASK, &omask, 0);
	errno = e;
	return status;
}

/* ---- identity ---- */

int setuid(uid_t u) { return (int)sys(SYS_setuid, u); }
int setgid(gid_t g) { return (int)sys(SYS_setgid, g); }
int seteuid(uid_t u) { return (int)sys(SYS_setresuid, -1, u, -1); }
int setegid(gid_t g) { return (int)sys(SYS_setresgid, -1, g, -1); }
int setreuid(uid_t r, uid_t e) { return (int)sys(SYS_setreuid, r, e); }
int setregid(gid_t r, gid_t e) { return (int)sys(SYS_setregid, r, e); }
int setresuid(uid_t r, uid_t e, uid_t s) { return (int)sys(SYS_setresuid, r, e, s); }
int setresgid(gid_t r, gid_t e, gid_t s) { return (int)sys(SYS_setresgid, r, e, s); }
int getresuid(uid_t *r, uid_t *e, uid_t *s) { return (int)sys(SYS_getresuid, r, e, s); }
int getresgid(gid_t *r, gid_t *e, gid_t *s) { return (int)sys(SYS_getresgid, r, e, s); }
int getgroups(int n, gid_t list[]) { return (int)sys(SYS_getgroups, n, list); }
int setgroups(size_t n, const gid_t *list) { return (int)sys(SYS_setgroups, n, list); }
pid_t getpgid(pid_t pid) { return (pid_t)sys(SYS_getpgid, pid); }
pid_t getpgrp(void) { return (pid_t)__sys(SYS_getpgid, 0); }
int setpgid(pid_t pid, pid_t pg) { return (int)sys(SYS_setpgid, pid, pg); }
pid_t setsid(void) { return (pid_t)sys(SYS_setsid); }
pid_t getsid(pid_t pid) { return (pid_t)sys(SYS_getsid, pid); }
int nice(int inc)
{
	long prio = __sys(SYS_getpriority, 0 /* PRIO_PROCESS */, 0);
	if (prio < 0)
		return (int)__syscall_ret((unsigned long)prio);
	/* the kernel returns 20 - nice */
	int n = 20 - (int)prio + inc;
	if (n > 19)
		n = 19;
	if (n < -20)
		n = -20;
	if (sys(SYS_setpriority, 0, 0, n) < 0)
		return -1;
	return n;
}
