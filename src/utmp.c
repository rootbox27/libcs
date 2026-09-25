/* utmp/utmpx: records in the glibc x86_64 format (384 bytes each). */
#include <utmp.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

_Static_assert(sizeof(struct utmpx) == 384, "utmp record size");

static char path[256] = "/var/run/utmp";
static int fd = -1;
static struct utmpx rec;
static volatile int lock;

int utmpname(const char *p)
{
	size_t n = strlen(p);
	if (n >= sizeof path) {
		errno = ENAMETOOLONG;
		return -1;
	}
	LOCK(lock);
	memcpy(path, p, n + 1);
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
	UNLOCK(lock);
	return 0;
}

void setutxent(void)
{
	LOCK(lock);
	if (fd >= 0)
		lseek(fd, 0, SEEK_SET);
	UNLOCK(lock);
}

void endutxent(void)
{
	LOCK(lock);
	if (fd >= 0)
		close(fd);
	fd = -1;
	UNLOCK(lock);
}

static int ensure_open(void)
{
	if (fd < 0)
		fd = open(path, O_RDONLY | O_CLOEXEC);
	return fd;
}

struct utmpx *getutxent(void)
{
	struct utmpx *r = 0;
	LOCK(lock);
	if (ensure_open() >= 0 && read(fd, &rec, sizeof rec) == (ssize_t)sizeof rec)
		r = &rec;
	UNLOCK(lock);
	return r;
}

static int id_match(const struct utmpx *u, const struct utmpx *key)
{
	switch (key->ut_type) {
	case RUN_LVL: case BOOT_TIME: case NEW_TIME: case OLD_TIME:
		return u->ut_type == key->ut_type;
	case INIT_PROCESS: case LOGIN_PROCESS: case USER_PROCESS: case DEAD_PROCESS:
		return (u->ut_type == INIT_PROCESS || u->ut_type == LOGIN_PROCESS || u->ut_type == USER_PROCESS ||
		        u->ut_type == DEAD_PROCESS) && !strncmp(u->ut_id, key->ut_id, sizeof u->ut_id);
	}
	return 0;
}

struct utmpx *getutxid(const struct utmpx *key)
{
	struct utmpx *u;
	while ((u = getutxent()))
		if (id_match(u, key))
			return u;
	return 0;
}

struct utmpx *getutxline(const struct utmpx *key)
{
	struct utmpx *u;
	while ((u = getutxent()))
		if ((u->ut_type == LOGIN_PROCESS || u->ut_type == USER_PROCESS) &&
		    !strncmp(u->ut_line, key->ut_line, sizeof u->ut_line))
			return u;
	return 0;
}

struct utmpx *pututxline(const struct utmpx *u)
{
	static struct utmpx copy;
	copy = *u;
	LOCK(lock);
	int w = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (w < 0) {
		UNLOCK(lock);
		return 0;
	}
	struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	fcntl(w, F_SETLKW, &fl);
	struct utmpx cur;
	off_t off = 0;
	while (pread(w, &cur, sizeof cur, off) == (ssize_t)sizeof cur) {
		if (id_match(&cur, &copy))
			break;
		off += (off_t)sizeof cur;
	}
	ssize_t n = pwrite(w, &copy, sizeof copy, off);
	fl.l_type = F_UNLCK;
	fcntl(w, F_SETLK, &fl);
	close(w);
	UNLOCK(lock);
	return n == (ssize_t)sizeof copy ? &copy : 0;
}

void updwtmp(const char *file, const struct utmp *u)
{
	int w = open(file, O_WRONLY | O_APPEND | O_CLOEXEC);
	if (w < 0)
		return;
	struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	fcntl(w, F_SETLKW, &fl);
	ssize_t r = write(w, u, sizeof *u);
	(void)r;
	fl.l_type = F_UNLCK;
	fcntl(w, F_SETLK, &fl);
	close(w);
}

void logwtmp(const char *line, const char *name, const char *host)
{
	struct utmp u;
	struct timeval tv;
	memset(&u, 0, sizeof u);
	u.ut_type = name && *name ? USER_PROCESS : DEAD_PROCESS;
	u.ut_pid = getpid();
	strncpy(u.ut_line, line, sizeof u.ut_line);
	strncpy(u.ut_user, name, sizeof u.ut_user);
	strncpy(u.ut_host, host, sizeof u.ut_host);
	gettimeofday(&tv, 0);
	u.ut_tv.tv_sec = (int)tv.tv_sec;
	u.ut_tv.tv_usec = (int)tv.tv_usec;
	updwtmp(_PATH_WTMP, &u);
}

void setutent(void) { setutxent(); }
void endutent(void) { endutxent(); }
struct utmp *getutent(void) { return getutxent(); }
struct utmp *getutid(const struct utmp *u) { return getutxid(u); }
struct utmp *getutline(const struct utmp *u) { return getutxline(u); }
struct utmp *pututline(const struct utmp *u) { return pututxline(u); }
