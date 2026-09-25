/* popen and pclose. */
#include "stdio_impl.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

FILE *popen(const char *cmd, const char *mode)
{
	/* "r" or "w", optionally followed by 'e' (close-on-exec) */
	if (!mode || (*mode != 'r' && *mode != 'w') || (mode[1] && strcmp(mode + 1, "e"))) {
		errno = EINVAL;
		return 0;
	}
	int reading = *mode == 'r';
	int p[2];
	/* both ends close-on-exec: the child dups its end to 0 or 1, and
	 * other popen streams are never inherited */
	if (pipe2(p, O_CLOEXEC))
		return 0;
	int mine = reading ? p[0] : p[1];
	int theirs = reading ? p[1] : p[0];
	int target = reading ? 1 : 0;

	pid_t pid = fork();
	if (pid < 0) {
		int e = errno;
		close(p[0]);
		close(p[1]);
		errno = e;
		return 0;
	}
	if (pid == 0) {
		if (theirs == target) {
			/* already in place: just clear close-on-exec */
			fcntl(theirs, F_SETFD, 0);
		} else if (dup2(theirs, target) < 0) {
			_exit(127);
		}
		char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
		execve("/bin/sh", argv, __environ);
		_exit(127);
	}
	close(theirs);
	if (!strchr(mode, 'e'))
		fcntl(mine, F_SETFD, 0);
	FILE *f = __fdopen_flags(mine, reading ? "r" : "w");
	if (!f) {
		int e = errno;
		close(mine);
		while (waitpid(pid, 0, 0) < 0 && errno == EINTR) ;
		errno = e;
		return 0;
	}
	f->pipe_pid = pid;
	return f;
}

int pclose(FILE *f)
{
	pid_t pid = f->pipe_pid;
	if (!pid) {
		errno = ECHILD;
		return -1;
	}
	fclose(f);
	int status;
	pid_t r;
	while ((r = waitpid(pid, &status, 0)) < 0 && errno == EINTR) ;
	return r < 0 ? -1 : status;
}
