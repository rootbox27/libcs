/* posix_spawn: clone(CLONE_VM | CLONE_VFORK) a child on a private stack,
 * apply the attributes and file actions with raw system calls (the child
 * shares our memory, including errno and the heap, so it must not touch
 * either), then exec. Failures travel back through a close-on-exec pipe.
 * Signal handlers are reset to SIG_DFL in the child before any signal can
 * be delivered, since running the parent's handlers there is unsafe. */
#include <spawn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "internal.h"

enum { FA_OPEN, FA_CLOSE, FA_DUP2, FA_CHDIR, FA_FCHDIR };

struct fa {
	int type, fd, newfd, oflag;
	mode_t mode;
	char *path;
};

/* ---- attributes ---- */

int posix_spawnattr_init(posix_spawnattr_t *a)
{
	memset(a, 0, sizeof *a);
	return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *a) { (void)a; return 0; }

int posix_spawnattr_getflags(const posix_spawnattr_t *restrict a, short *restrict f)
{
	*f = (short)a->__flags;
	return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *a, short f)
{
	if ((unsigned short)f & ~0xffu)
		return EINVAL;
	a->__flags = f;
	return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict a, pid_t *restrict p) { *p = a->__pgrp; return 0; }
int posix_spawnattr_setpgroup(posix_spawnattr_t *a, pid_t p) { a->__pgrp = p; return 0; }
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict a, sigset_t *restrict s) { *s = a->__def; return 0; }
int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict a, const sigset_t *restrict s) { a->__def = *s; return 0; }
int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict a, sigset_t *restrict s) { *s = a->__mask; return 0; }
int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict a, const sigset_t *restrict s) { a->__mask = *s; return 0; }

int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict a, struct sched_param *restrict p)
{
	p->sched_priority = a->__prio;
	return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict a, const struct sched_param *restrict p)
{
	a->__prio = p->sched_priority;
	return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict a, int *restrict p) { *p = a->__pol; return 0; }
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *a, int p) { a->__pol = p; return 0; }

/* ---- file actions ---- */

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *f)
{
	f->__n = f->__cap = 0;
	f->__actions = 0;
	return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *f)
{
	struct fa *a = f->__actions;
	for (int i = 0; i < f->__n; i++)
		free(a[i].path);
	free(a);
	f->__n = f->__cap = 0;
	f->__actions = 0;
	return 0;
}

static struct fa *add(posix_spawn_file_actions_t *f)
{
	if (f->__n == f->__cap) {
		int cap = f->__cap ? 2 * f->__cap : 4;
		struct fa *a = realloc(f->__actions, (size_t)cap * sizeof *a);
		if (!a)
			return 0;
		f->__actions = a;
		f->__cap = cap;
	}
	struct fa *a = (struct fa *)f->__actions + f->__n;
	memset(a, 0, sizeof *a);
	return a;
}

static int bad_fd(int fd) { return fd < 0 || fd >= INT_MAX; }

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *restrict f, int fd, const char *restrict path, int oflag, mode_t mode)
{
	if (bad_fd(fd))
		return EBADF;
	struct fa *a = add(f);
	char *p = strdup(path);
	if (!a || !p) {
		free(p);
		return ENOMEM;
	}
	*a = (struct fa){ FA_OPEN, fd, 0, oflag, mode, p };
	f->__n++;
	return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *f, int fd)
{
	if (bad_fd(fd))
		return EBADF;
	struct fa *a = add(f);
	if (!a)
		return ENOMEM;
	a->type = FA_CLOSE;
	a->fd = fd;
	f->__n++;
	return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *f, int fd, int newfd)
{
	if (bad_fd(fd) || bad_fd(newfd))
		return EBADF;
	struct fa *a = add(f);
	if (!a)
		return ENOMEM;
	a->type = FA_DUP2;
	a->fd = fd;
	a->newfd = newfd;
	f->__n++;
	return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *restrict f, const char *restrict path)
{
	struct fa *a = add(f);
	char *p = strdup(path);
	if (!a || !p) {
		free(p);
		return ENOMEM;
	}
	a->type = FA_CHDIR;
	a->path = p;
	f->__n++;
	return 0;
}

int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *f, int fd)
{
	if (bad_fd(fd))
		return EBADF;
	struct fa *a = add(f);
	if (!a)
		return ENOMEM;
	a->type = FA_FCHDIR;
	a->fd = fd;
	f->__n++;
	return 0;
}

/* ---- spawning ---- */

struct args {
	const char *file;
	char *const *argv, *const *envp;
	const posix_spawn_file_actions_t *fa;
	const posix_spawnattr_t *attr;
	const char *path; /* PATH to search, or 0 */
	int pipe;
	sigset_t oldmask;
};

static void fail(struct args *a, int err)
{
	__sys(SYS_write, a->pipe, &err, sizeof err);
	__sys(SYS_exit, 127);
	for (;;) ;
}

static int child(void *p)
{
	struct args *a = p;
	const posix_spawnattr_t *at = a->attr;
	int flags = at ? at->__flags : 0;
	long r;

	/* signals: nothing may run a parent handler in here */
	for (int sig = 1; sig < 65; sig++) {
		struct k_sigaction sa = { 0 };
		if (sig == SIGKILL || sig == SIGSTOP)
			continue;
		if ((flags & POSIX_SPAWN_SETSIGDEF) && sigismember(&at->__def, sig) == 1) {
			sa.handler = SIG_DFL;
		} else {
			__sys(SYS_rt_sigaction, sig, 0, &sa, 8);
			if (sa.handler == SIG_IGN || sa.handler == SIG_DFL)
				continue;
			sa = (struct k_sigaction){ 0 };
			sa.handler = SIG_DFL;
		}
		__sys(SYS_rt_sigaction, sig, &sa, 0, 8);
	}

	if (flags & POSIX_SPAWN_SETSID) {
		if ((r = __sys(SYS_setsid)) < 0)
			fail(a, (int)-r);
	}
	if (flags & POSIX_SPAWN_SETPGROUP) {
		if ((r = __sys(SYS_setpgid, 0, at->__pgrp)) < 0)
			fail(a, (int)-r);
	}
	if (flags & POSIX_SPAWN_SETSCHEDULER) {
		struct sched_param sp = { at->__prio };
		if ((r = __sys(SYS_sched_setscheduler, 0, at->__pol, &sp)) < 0)
			fail(a, (int)-r);
	} else if (flags & POSIX_SPAWN_SETSCHEDPARAM) {
		struct sched_param sp = { at->__prio };
		if ((r = __sys(SYS_sched_setparam, 0, &sp)) < 0)
			fail(a, (int)-r);
	}
	if (flags & POSIX_SPAWN_RESETIDS) {
		if ((r = __sys(SYS_setresgid, -1, __sys(SYS_getgid), -1)) < 0 ||
		    (r = __sys(SYS_setresuid, -1, __sys(SYS_getuid), -1)) < 0)
			fail(a, (int)-r);
	}

	if (a->fa) {
		struct fa *f = a->fa->__actions;
		for (int i = 0; i < a->fa->__n; i++) {
			/* keep the error pipe out of the way of the actions */
			int target = f[i].type == FA_DUP2 ? f[i].newfd : f[i].fd;
			if (target == a->pipe && f[i].type != FA_CHDIR) {
				r = __sys(SYS_fcntl, a->pipe, F_DUPFD_CLOEXEC, 0);
				if (r < 0)
					fail(a, (int)-r);
				a->pipe = (int)r;
			}
			switch (f[i].type) {
			case FA_CLOSE:
				__sys(SYS_close, f[i].fd);
				break;
			case FA_DUP2:
				if (f[i].fd == f[i].newfd) {
					r = __sys(SYS_fcntl, f[i].fd, F_GETFD);
					if (r < 0 || (r = __sys(SYS_fcntl, f[i].fd, F_SETFD, r & ~FD_CLOEXEC)) < 0)
						fail(a, (int)-r);
				} else if ((r = __sys(SYS_dup2, f[i].fd, f[i].newfd)) < 0) {
					fail(a, (int)-r);
				}
				break;
			case FA_OPEN:
				r = __sys(SYS_openat, AT_FDCWD, f[i].path, f[i].oflag, f[i].mode);
				if (r < 0)
					fail(a, (int)-r);
				if (r != f[i].fd) {
					long d = __sys(SYS_dup2, r, f[i].fd);
					__sys(SYS_close, r);
					if (d < 0)
						fail(a, (int)-d);
				}
				break;
			case FA_CHDIR:
				if ((r = __sys(SYS_chdir, f[i].path)) < 0)
					fail(a, (int)-r);
				break;
			case FA_FCHDIR:
				if ((r = __sys(SYS_fchdir, f[i].fd)) < 0)
					fail(a, (int)-r);
				break;
			}
		}
	}

	const sigset_t *mask = (flags & POSIX_SPAWN_SETSIGMASK) ? &at->__mask : &a->oldmask;
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, mask, 0, 8);

	if (!a->path) {
		r = __sys(SYS_execve, a->file, a->argv, a->envp);
		fail(a, (int)-r);
	}
	/* posix_spawnp: search PATH like execvp */
	char buf[PATH_MAX];
	size_t fl = strlen(a->file);
	int seen_eacces = 0;
	r = -ENOENT;
	for (const char *d = a->path;; d++) {
		const char *e = d;
		while (*e && *e != ':')
			e++;
		size_t dl = (size_t)(e - d);
		if (dl + 1 + fl < sizeof buf) {
			memcpy(buf, d, dl);
			size_t n = dl;
			if (n)
				buf[n++] = '/';
			memcpy(buf + n, a->file, fl + 1);
			r = __sys(SYS_execve, buf, a->argv, a->envp);
			if (r == -EACCES)
				seen_eacces = 1;
			else if (r != -ENOENT && r != -ENOTDIR)
				fail(a, (int)-r);
		}
		if (!*e)
			break;
		d = e;
	}
	fail(a, seen_eacces ? EACCES : (int)-r);
	return 0;
}

static int spawn(pid_t *restrict pid, const char *file, const char *path,
                 const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                 char *const argv[], char *const envp[])
{
	struct args a = { file, argv, envp, fa, attr, path, -1, { { 0 } } };
	int p[2];
	if (pipe2(p, O_CLOEXEC) < 0)
		return errno;
	a.pipe = p[1];

	sigset_t all;
	sigfillset(&all);
	__sys(SYS_rt_sigprocmask, SIG_BLOCK, &all, &a.oldmask, 8);

	/* 16-byte aligned private stack for the child; the parent is suspended
	 * until the child execs or exits (CLONE_VFORK) */
	_Alignas(16) char stack[PATH_MAX + 8192];
	int r = __clone(child, stack + sizeof stack, CLONE_VM | CLONE_VFORK | SIGCHLD, &a, 0, 0, 0);
	close(p[1]);
	int err = 0;
	if (r > 0) {
		long n;
		do n = __sys(SYS_read, p[0], &err, sizeof err);
		while (n == -EINTR);
		if (n != sizeof err) {
			err = 0;
		} else {
			int st;
			while (waitpid(r, &st, 0) < 0 && errno == EINTR)
				;
		}
	} else {
		err = -r;
	}
	close(p[0]);
	__sys(SYS_rt_sigprocmask, SIG_SETMASK, &a.oldmask, 0, 8);
	if (!err && pid)
		*pid = r;
	return err;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path, const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *restrict attr, char *const argv[restrict], char *const envp[restrict])
{
	return spawn(pid, path, 0, fa, attr, argv, envp);
}

int posix_spawnp(pid_t *restrict pid, const char *restrict file, const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *restrict attr, char *const argv[restrict], char *const envp[restrict])
{
	if (strchr(file, '/'))
		return spawn(pid, file, 0, fa, attr, argv, envp);
	const char *path = getenv("PATH");
	if (!path)
		path = "/usr/local/bin:/bin:/usr/bin";
	return spawn(pid, file, path, fa, attr, argv, envp);
}
