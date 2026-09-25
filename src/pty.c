/* Pseudo-terminals (Linux devpts). */
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

int posix_openpt(int flags)
{
	int fd = open("/dev/ptmx", flags);
	if (fd < 0 && errno == ENOSPC)
		errno = EAGAIN;
	return fd;
}

int grantpt(int fd)
{
	/* devpts sets the slave's owner and mode itself; only validate */
	unsigned n;
	return ioctl(fd, TIOCGPTN, &n) < 0 ? (errno = EINVAL, -1) : 0;
}

int unlockpt(int fd)
{
	int unlock = 0;
	return ioctl(fd, TIOCSPTLCK, &unlock);
}

int ptsname_r(int fd, char *buf, size_t len)
{
	unsigned n;
	int saved = errno;
	if (ioctl(fd, TIOCGPTN, &n) < 0) {
		int e = errno == ENOTTY ? ENOTTY : errno;
		errno = saved;
		return e;
	}
	if ((size_t)snprintf(buf, len, "/dev/pts/%u", n) >= len)
		return ERANGE;
	return 0;
}

char *ptsname(int fd)
{
	static char buf[32];
	int e = ptsname_r(fd, buf, sizeof buf);
	if (e) {
		errno = e;
		return 0;
	}
	return buf;
}

int openpty(int *pm, int *ps, char *name, const struct termios *tio, const struct winsize *ws)
{
	char buf[32];
	int m = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (m < 0)
		return -1;
	int e = unlockpt(m) < 0 ? errno : ptsname_r(m, buf, sizeof buf);
	if (e) {
		close(m);
		errno = e;
		return -1;
	}
	int s = open(buf, O_RDWR | O_NOCTTY);
	if (s < 0) {
		int saved = errno;
		close(m);
		errno = saved;
		return -1;
	}
	/* the master stays close-on-exec only where the caller wants it */
	fcntl(m, F_SETFD, 0);
	if (tio)
		tcsetattr(s, TCSANOW, tio);
	if (ws)
		ioctl(s, TIOCSWINSZ, ws);
	if (name)
		strcpy(name, buf);
	*pm = m;
	*ps = s;
	return 0;
}

int login_tty(int fd)
{
	setsid();
	if (ioctl(fd, TIOCSCTTY, 0) < 0)
		return -1;
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2)
		close(fd);
	return 0;
}

pid_t forkpty(int *pm, char *name, const struct termios *tio, const struct winsize *ws)
{
	int m, s;
	if (openpty(&m, &s, name, tio, ws) < 0)
		return -1;
	pid_t pid = fork();
	if (pid < 0) {
		close(m);
		close(s);
		return -1;
	}
	if (!pid) {
		close(m);
		if (login_tty(s) < 0)
			_exit(1);
		return 0;
	}
	close(s);
	*pm = m;
	return pid;
}
