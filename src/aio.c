/* POSIX AIO on threads: each request runs on its own worker thread, with
 * every signal blocked so asynchronous signals still reach the program's
 * threads. The worker's last access to the caller's aiocb is the release
 * store of its status; after that the caller may free it. aio_fsync waits
 * for the requests on the same descriptor that were submitted before it. */
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "internal.h"

#define MAX_LIST 65536

struct group {
	int left;
	struct sigevent sev;
	sigset_t mask;
};

struct req {
	struct aiocb *cb;
	int fd, op;
	unsigned long seq;
	struct req *next, *prev;
	struct group *grp;
	struct sigevent sev;
	sigset_t mask; /* the submitter's, for SIGEV_THREAD callbacks */
};

enum { OP_READ, OP_WRITE, OP_FSYNC, OP_FDATASYNC };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv;
static pthread_once_t cv_once = PTHREAD_ONCE_INIT;
static struct req *head;
static unsigned long next_seq;

static void cv_init(void)
{
	pthread_condattr_t a;
	pthread_condattr_init(&a);
	pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
	pthread_cond_init(&cv, &a);
	pthread_condattr_destroy(&a);
}

static int status(const struct aiocb *cb)
{
	return __atomic_load_n(&cb->__err, __ATOMIC_ACQUIRE);
}

static int valid_sev(const struct sigevent *sev)
{
	if (!sev)
		return 1;
	switch (sev->sigev_notify) {
	case SIGEV_NONE:
		return 1;
	case SIGEV_SIGNAL:
		/* signal 0, as in a zeroed aiocb, means no notification */
		return sev->sigev_signo >= 0 && sev->sigev_signo < _NSIG;
	case SIGEV_THREAD_ID:
		return sev->sigev_signo > 0 && sev->sigev_signo < _NSIG;
	case SIGEV_THREAD:
		return sev->sigev_notify_function != 0;
	}
	return 0;
}

static void notify(const struct sigevent *sev, const sigset_t *mask)
{
	switch (sev->sigev_notify) {
	case SIGEV_SIGNAL:
	case SIGEV_THREAD_ID: {
		if (!sev->sigev_signo)
			break;
		siginfo_t si;
		memset(&si, 0, sizeof si);
		si.si_signo = sev->sigev_signo;
		si.si_code = SI_ASYNCIO;
		si.si_pid = getpid();
		si.si_uid = getuid();
		si.si_value = sev->sigev_value;
		if (sev->sigev_notify == SIGEV_SIGNAL)
			__sys(SYS_rt_sigqueueinfo, si.si_pid, si.si_signo, &si);
		else
			__sys(SYS_rt_tgsigqueueinfo, si.si_pid, sev->sigev_notify_thread_id, si.si_signo, &si);
		break;
	}
	case SIGEV_THREAD:
		/* run the callback with the submitter's signal mask */
		pthread_sigmask(SIG_SETMASK, mask, 0);
		sev->sigev_notify_function(sev->sigev_value);
		sigset_t all;
		sigfillset(&all);
		pthread_sigmask(SIG_SETMASK, &all, 0);
		break;
	}
}

static int earlier_on_fd(const struct req *r)
{
	for (struct req *p = head; p; p = p->next)
		if (p->fd == r->fd && p->seq < r->seq)
			return 1;
	return 0;
}

static void *worker(void *arg)
{
	struct req *r = arg;
	struct aiocb *cb = r->cb;
	ssize_t ret;
	int err = 0;
	if (r->op == OP_FSYNC || r->op == OP_FDATASYNC) {
		pthread_mutex_lock(&lock);
		while (earlier_on_fd(r))
			pthread_cond_wait(&cv, &lock);
		pthread_mutex_unlock(&lock);
		ret = r->op == OP_FSYNC ? fsync(r->fd) : fdatasync(r->fd);
	} else {
		void *buf = (void *)cb->aio_buf;
		size_t n = cb->aio_nbytes;
		off_t off = cb->aio_offset;
		if (r->op == OP_READ) {
			ret = pread(r->fd, buf, n, off);
			if (ret < 0 && errno == ESPIPE)
				ret = read(r->fd, buf, n);
		} else {
			ret = pwrite(r->fd, buf, n, off);
			if (ret < 0 && errno == ESPIPE)
				ret = write(r->fd, buf, n);
		}
	}
	if (ret < 0)
		err = errno;

	struct group *done_grp = 0;
	pthread_mutex_lock(&lock);
	cb->__ret = ret;
	__atomic_store_n(&cb->__err, err, __ATOMIC_RELEASE);
	/* cb belongs to the caller again from here on */
	if (r->prev)
		r->prev->next = r->next;
	else
		head = r->next;
	if (r->next)
		r->next->prev = r->prev;
	if (r->grp && --r->grp->left == 0)
		done_grp = r->grp;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&lock);

	notify(&r->sev, &r->mask);
	if (done_grp) {
		notify(&done_grp->sev, &done_grp->mask);
		free(done_grp);
	}
	free(r);
	return 0;
}

static int submit(struct aiocb *cb, int op, struct group *grp)
{
	pthread_once(&cv_once, cv_init);
	int fl = fcntl(cb->aio_fildes, F_GETFL);
	if (fl < 0) {
		errno = EBADF;
		return -1;
	}
	int acc = fl & O_ACCMODE;
	if ((op == OP_READ && acc == O_WRONLY) || (op == OP_WRITE && acc == O_RDONLY)) {
		errno = EBADF;
		return -1;
	}
	if (cb->aio_reqprio < 0 || !valid_sev(&cb->aio_sigevent) ||
	    ((op == OP_READ || op == OP_WRITE) && (cb->aio_nbytes > SSIZE_MAX || cb->aio_offset < 0))) {
		errno = EINVAL;
		return -1;
	}
	struct req *r = calloc(1, sizeof *r);
	if (!r) {
		errno = EAGAIN;
		return -1;
	}
	r->cb = cb;
	r->fd = cb->aio_fildes;
	r->op = op;
	r->grp = grp;
	r->sev = cb->aio_sigevent;
	/* workers start with every signal blocked; the caller's mask is
	 * kept in a local because a fast worker may free r before
	 * pthread_create even returns */
	sigset_t all, saved;
	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, &saved);
	r->mask = saved;

	cb->__ret = 0;
	__atomic_store_n(&cb->__err, EINPROGRESS, __ATOMIC_RELEASE);
	pthread_mutex_lock(&lock);
	r->seq = next_seq++;
	r->next = head;
	if (head)
		head->prev = r;
	head = r;
	if (grp)
		grp->left++;
	pthread_mutex_unlock(&lock);

	pthread_attr_t a;
	pthread_attr_init(&a);
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	pthread_t t;
	int e = pthread_create(&t, &a, worker, r);
	pthread_attr_destroy(&a);
	pthread_sigmask(SIG_SETMASK, &saved, 0);
	if (e) {
		pthread_mutex_lock(&lock);
		if (r->prev)
			r->prev->next = r->next;
		else
			head = r->next;
		if (r->next)
			r->next->prev = r->prev;
		if (grp)
			grp->left--;
		pthread_cond_broadcast(&cv);
		pthread_mutex_unlock(&lock);
		free(r);
		cb->__ret = -1;
		__atomic_store_n(&cb->__err, EAGAIN, __ATOMIC_RELEASE);
		errno = EAGAIN;
		return -1;
	}
	return 0;
}

int aio_read(struct aiocb *cb)
{
	return submit(cb, OP_READ, 0);
}

int aio_write(struct aiocb *cb)
{
	return submit(cb, OP_WRITE, 0);
}

int aio_fsync(int op, struct aiocb *cb)
{
	if (op != O_SYNC && op != O_DSYNC) {
		errno = EINVAL;
		return -1;
	}
	return submit(cb, op == O_SYNC ? OP_FSYNC : OP_FDATASYNC, 0);
}

int aio_error(const struct aiocb *cb)
{
	return status(cb);
}

ssize_t aio_return(struct aiocb *cb)
{
	int e = status(cb);
	if (e == EINPROGRESS) {
		errno = EINVAL;
		return -1;
	}
	if (e)
		errno = e;
	return cb->__ret;
}

int aio_cancel(int fd, struct aiocb *cb)
{
	if (fcntl(fd, F_GETFD) < 0) {
		errno = EBADF;
		return -1;
	}
	if (cb && cb->aio_fildes != fd) {
		errno = EINVAL;
		return -1;
	}
	/* requests start as soon as they are submitted, so none can be
	 * withdrawn: they are either running or done */
	int r = AIO_ALLDONE;
	pthread_mutex_lock(&lock);
	if (cb) {
		if (status(cb) == EINPROGRESS)
			r = AIO_NOTCANCELED;
	} else {
		for (struct req *p = head; p; p = p->next)
			if (p->fd == fd)
				r = AIO_NOTCANCELED;
	}
	pthread_mutex_unlock(&lock);
	return r;
}

static int any_done(const struct aiocb *const list[], int n)
{
	for (int i = 0; i < n; i++)
		if (list[i] && status(list[i]) != EINPROGRESS)
			return 1;
	return 0;
}

int aio_suspend(const struct aiocb *const list[], int n, const struct timespec *timeout)
{
	pthread_once(&cv_once, cv_init);
	if (n < 0) {
		errno = EINVAL;
		return -1;
	}
	struct timespec end;
	if (timeout) {
		if (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000 || timeout->tv_sec < 0) {
			errno = EINVAL;
			return -1;
		}
		clock_gettime(CLOCK_MONOTONIC, &end);
		end.tv_sec += timeout->tv_sec;
		end.tv_nsec += timeout->tv_nsec;
		if (end.tv_nsec >= 1000000000) {
			end.tv_sec++;
			end.tv_nsec -= 1000000000;
		}
	}
	int r = 0;
	pthread_mutex_lock(&lock);
	while (!any_done(list, n)) {
		if (timeout) {
			if (pthread_cond_timedwait(&cv, &lock, &end) == ETIMEDOUT && !any_done(list, n)) {
				r = -1;
				break;
			}
		} else {
			pthread_cond_wait(&cv, &lock);
		}
	}
	pthread_mutex_unlock(&lock);
	if (r)
		errno = EAGAIN;
	return r;
}

int lio_listio(int mode, struct aiocb *restrict const list[restrict], int n, struct sigevent *restrict sev)
{
	if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || n < 0 || n > MAX_LIST ||
	    (mode == LIO_NOWAIT && !valid_sev(sev))) {
		errno = EINVAL;
		return -1;
	}
	struct group *grp = 0;
	if (mode == LIO_NOWAIT && sev && sev->sigev_notify != SIGEV_NONE) {
		grp = malloc(sizeof *grp);
		if (!grp) {
			errno = EAGAIN;
			return -1;
		}
		grp->left = 1; /* held until every request is submitted */
		grp->sev = *sev;
		pthread_sigmask(SIG_BLOCK, 0, &grp->mask);
	}
	int failed = 0;
	for (int i = 0; i < n; i++) {
		struct aiocb *cb = list[i];
		if (!cb || cb->aio_lio_opcode == LIO_NOP)
			continue;
		int op = cb->aio_lio_opcode == LIO_READ ? OP_READ : cb->aio_lio_opcode == LIO_WRITE ? OP_WRITE : -1;
		if (op < 0 || submit(cb, op, grp)) {
			if (op < 0)
				errno = EINVAL;
			cb->__ret = -1;
			__atomic_store_n(&cb->__err, errno, __ATOMIC_RELEASE);
			failed = 1;
		}
	}
	if (grp) {
		pthread_mutex_lock(&lock);
		int last = --grp->left == 0;
		pthread_mutex_unlock(&lock);
		if (last) {
			notify(&grp->sev, &grp->mask);
			free(grp);
		}
	}
	if (mode == LIO_WAIT) {
		pthread_once(&cv_once, cv_init);
		pthread_mutex_lock(&lock);
		for (int i = 0; i < n; i++)
			while (list[i] && list[i]->aio_lio_opcode != LIO_NOP && status(list[i]) == EINPROGRESS)
				pthread_cond_wait(&cv, &lock);
		pthread_mutex_unlock(&lock);
		for (int i = 0; i < n; i++)
			if (list[i] && list[i]->aio_lio_opcode != LIO_NOP && status(list[i]))
				failed = 1;
		if (failed) {
			errno = EIO;
			return -1;
		}
		return 0;
	}
	if (failed) {
		errno = EAGAIN;
		return -1;
	}
	return 0;
}
