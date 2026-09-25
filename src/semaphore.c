/* POSIX semaphores. __val[0] is the count, __val[1] the number of
 * waiters, __val[2] nonzero for a process-private semaphore. Named
 * semaphores are files under /dev/shm mapped shared. */
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "internal.h"

int sem_init(sem_t *s, int pshared, unsigned value)
{
	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return -1;
	}
	memset((void *)s, 0, sizeof *s);
	s->__val[0] = (int)value;
	s->__val[2] = !pshared;
	return 0;
}

int sem_destroy(sem_t *s)
{
	(void)s;
	return 0;
}

int sem_trywait(sem_t *s)
{
	int v = s->__val[0];
	while (v > 0) {
		if (__atomic_compare_exchange_n(&s->__val[0], &v, v - 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return 0;
	}
	errno = EAGAIN;
	return -1;
}

int sem_clockwait(sem_t *restrict s, clockid_t clk, const struct timespec *restrict abs)
{
	if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC) {
		errno = EINVAL;
		return -1;
	}
	__testcancel();
	for (;;) {
		int v = s->__val[0];
		if (v > 0) {
			if (__atomic_compare_exchange_n(&s->__val[0], &v, v - 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
				return 0;
			continue;
		}
		if (abs && (abs->tv_nsec < 0 || abs->tv_nsec >= 1000000000)) {
			errno = EINVAL;
			return -1;
		}
		__atomic_fetch_add(&s->__val[1], 1, __ATOMIC_SEQ_CST);
		int r = __futex_timedwait(&s->__val[0], v, clk, abs, s->__val[2]);
		__atomic_fetch_sub(&s->__val[1], 1, __ATOMIC_SEQ_CST);
		if (r == -ETIMEDOUT || r == -EINTR) {
			if (r == -EINTR)
				__testcancel();
			errno = -r;
			return -1;
		}
	}
}

int sem_timedwait(sem_t *restrict s, const struct timespec *restrict abs)
{
	return sem_clockwait(s, CLOCK_REALTIME, abs);
}

int sem_wait(sem_t *s)
{
	return sem_clockwait(s, CLOCK_REALTIME, 0);
}

int sem_post(sem_t *s)
{
	int v = s->__val[0];
	do {
		if (v == SEM_VALUE_MAX) {
			errno = EOVERFLOW;
			return -1;
		}
	} while (!__atomic_compare_exchange_n(&s->__val[0], &v, v + 1, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED));
	if (__atomic_load_n(&s->__val[1], __ATOMIC_SEQ_CST))
		__sys(SYS_futex, &s->__val[0], 1 | (s->__val[2] ? 128 : 0), 1);
	return 0;
}

int sem_getvalue(sem_t *restrict s, int *restrict v)
{
	int x = s->__val[0];
	*v = x < 0 ? 0 : x;
	return 0;
}

/* ---- named semaphores ---- */

/* one mapping per semaphore file, shared by repeated sem_open calls */
static struct named {
	struct named *next;
	dev_t dev;
	ino_t ino;
	sem_t *sem;
	int refs;
} *named_list;
static volatile int named_lock;

/* "/name" -> "/dev/shm/sem.name" */
static int sem_path(const char *name, char *buf)
{
	while (*name == '/')
		name++;
	size_t n = strlen(name);
	if (!n || n > NAME_MAX - 4 || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) {
		errno = n > NAME_MAX - 4 ? ENAMETOOLONG : EINVAL;
		return -1;
	}
	memcpy(buf, "/dev/shm/sem.", 13);
	memcpy(buf + 13, name, n + 1);
	return 0;
}

static sem_t *map_fd(int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
		return SEM_FAILED;
	LOCK(named_lock);
	for (struct named *p = named_list; p; p = p->next) {
		if (p->dev == st.st_dev && p->ino == st.st_ino) {
			p->refs++;
			UNLOCK(named_lock);
			return p->sem;
		}
	}
	struct named *p = malloc(sizeof *p);
	void *m = p ? mmap(0, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
	if (m == MAP_FAILED) {
		free(p);
		UNLOCK(named_lock);
		if (!errno)
			errno = ENOMEM;
		return SEM_FAILED;
	}
	p->dev = st.st_dev;
	p->ino = st.st_ino;
	p->sem = m;
	p->refs = 1;
	p->next = named_list;
	named_list = p;
	UNLOCK(named_lock);
	return m;
}

sem_t *sem_open(const char *name, int oflag, ...)
{
	char path[NAME_MAX + 16];
	if (sem_path(name, path) < 0)
		return SEM_FAILED;
	mode_t mode = 0;
	unsigned value = 0;
	if (oflag & O_CREAT) {
		va_list ap;
		va_start(ap, oflag);
		mode = va_arg(ap, mode_t);
		value = va_arg(ap, unsigned);
		va_end(ap);
		if (value > SEM_VALUE_MAX) {
			errno = EINVAL;
			return SEM_FAILED;
		}
	}
	for (;;) {
		int fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
		if (fd >= 0) {
			if (oflag & O_CREAT && oflag & O_EXCL) {
				close(fd);
				errno = EEXIST;
				return SEM_FAILED;
			}
			sem_t *s = map_fd(fd);
			close(fd);
			return s;
		}
		if (errno != ENOENT || !(oflag & O_CREAT))
			return SEM_FAILED;
		/* Create under a temporary name, initialise, then link into place so
		 * nobody sees an uninitialised semaphore. */
		char tmp[40];
		unsigned char rnd[8];
		__secure_random(rnd, sizeof rnd);
		memcpy(tmp, "/dev/shm/sem.tmp-", 17);
		for (int i = 0; i < 8; i++) {
			tmp[17 + 2 * i] = "0123456789abcdef"[rnd[i] >> 4];
			tmp[18 + 2 * i] = "0123456789abcdef"[rnd[i] & 15];
		}
		tmp[33] = 0;
		fd = open(tmp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
		if (fd < 0)
			return SEM_FAILED;
		sem_t init;
		sem_init(&init, 1, value);
		int ok = write(fd, &init, sizeof init) == (ssize_t)sizeof init;
		if (ok && link(tmp, path) == 0) {
			unlink(tmp);
			sem_t *s = map_fd(fd);
			close(fd);
			return s;
		}
		int e = errno;
		unlink(tmp);
		close(fd);
		if (!ok || e != EEXIST) {
			errno = ok ? e : EIO;
			return SEM_FAILED;
		}
		if (oflag & O_EXCL) {
			errno = EEXIST;
			return SEM_FAILED;
		}
		/* someone else created it first: open theirs */
	}
}

int sem_close(sem_t *s)
{
	LOCK(named_lock);
	for (struct named **pp = &named_list; *pp; pp = &(*pp)->next) {
		struct named *p = *pp;
		if (p->sem != s)
			continue;
		if (--p->refs == 0) {
			*pp = p->next;
			munmap((void *)s, sizeof(sem_t));
			free(p);
		}
		UNLOCK(named_lock);
		return 0;
	}
	UNLOCK(named_lock);
	errno = EINVAL;
	return -1;
}

int sem_unlink(const char *name)
{
	char path[NAME_MAX + 16];
	if (sem_path(name, path) < 0)
		return -1;
	return unlink(path);
}
