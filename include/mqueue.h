#ifndef _MQUEUE_H
#define _MQUEUE_H
#include <features.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
__BEGIN_DECLS
typedef int mqd_t;
struct mq_attr {
	long mq_flags, mq_maxmsg, mq_msgsize, mq_curmsgs, __unused[4];
};
mqd_t mq_open(const char *, int, ...);
int mq_close(mqd_t);
int mq_unlink(const char *);
int mq_send(mqd_t, const char *, size_t, unsigned);
int mq_timedsend(mqd_t, const char *, size_t, unsigned, const struct timespec *);
ssize_t mq_receive(mqd_t, char *, size_t, unsigned *);
ssize_t mq_timedreceive(mqd_t, char *__restrict, size_t, unsigned *__restrict, const struct timespec *__restrict);
int mq_getattr(mqd_t, struct mq_attr *);
int mq_setattr(mqd_t, const struct mq_attr *__restrict, struct mq_attr *__restrict);
int mq_notify(mqd_t, const struct sigevent *);
__END_DECLS
#endif
