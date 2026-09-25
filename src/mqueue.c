/* POSIX message queues. mq_notify supports SIGEV_SIGNAL and SIGEV_NONE;
 * SIGEV_THREAD is not provided (ENOTSUP). */
#include <mqueue.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include "internal.h"

mqd_t mq_open(const char *name, int flags, ...)
{
	mode_t mode = 0;
	struct mq_attr *attr = 0;
	if (*name == '/')
		name++;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
	}
	return (mqd_t)sys(SYS_mq_open, name, flags | O_CLOEXEC, mode, attr);
}

int mq_close(mqd_t q) { return close(q); }

int mq_unlink(const char *name)
{
	if (*name == '/')
		name++;
	long r = __sys(SYS_mq_unlink, name);
	if (r == -EPERM)
		r = -EACCES; /* POSIX's error for this */
	return (int)__syscall_ret((unsigned long)r);
}

int mq_timedsend(mqd_t q, const char *msg, size_t n, unsigned prio, const struct timespec *at)
{
	return (int)sys(SYS_mq_timedsend, q, msg, n, prio, at);
}

int mq_send(mqd_t q, const char *msg, size_t n, unsigned prio) { return mq_timedsend(q, msg, n, prio, 0); }

ssize_t mq_timedreceive(mqd_t q, char *restrict msg, size_t n, unsigned *restrict prio, const struct timespec *restrict at)
{
	return sys(SYS_mq_timedreceive, q, msg, n, prio, at);
}

ssize_t mq_receive(mqd_t q, char *msg, size_t n, unsigned *prio) { return mq_timedreceive(q, msg, n, prio, 0); }

int mq_setattr(mqd_t q, const struct mq_attr *restrict nw, struct mq_attr *restrict old)
{
	return (int)sys(SYS_mq_getsetattr, q, nw, old);
}

int mq_getattr(mqd_t q, struct mq_attr *attr) { return mq_setattr(q, 0, attr); }

int mq_notify(mqd_t q, const struct sigevent *ev)
{
	if (ev && ev->sigev_notify == SIGEV_THREAD) {
		errno = ENOTSUP;
		return -1;
	}
	return (int)sys(SYS_mq_notify, q, ev);
}
