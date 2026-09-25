/* <aio.h> */
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include "harness.h"

static void wait_one(struct aiocb *cb)
{
	const struct aiocb *l[1] = { cb };
	while (aio_error(cb) == EINPROGRESS)
		aio_suspend(l, 1, 0);
}

static volatile int cb_hits;
static void on_done(union sigval v)
{
	__atomic_add_fetch(&cb_hits, v.sival_int, __ATOMIC_SEQ_CST);
}

int main(void)
{
	char path[] = "/tmp/citadel-aioXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	unlink(path);

	/* write, fsync, read back */
	static char data[100000];
	for (size_t i = 0; i < sizeof data; i++)
		data[i] = (char)(i * 7);
	struct aiocb w = { .aio_fildes = fd, .aio_buf = data, .aio_nbytes = sizeof data, .aio_offset = 4096 };
	w.aio_sigevent.sigev_notify = SIGEV_NONE;
	CHECK(aio_write(&w) == 0);
	struct aiocb s = { .aio_fildes = fd };
	CHECK(aio_fsync(O_SYNC, &s) == 0);
	wait_one(&s);
	/* the fsync waited for the write submitted before it */
	CHECK(aio_error(&w) == 0 && aio_return(&w) == (ssize_t)sizeof data);
	CHECK(aio_error(&s) == 0 && aio_return(&s) == 0);

	static char back[100000];
	struct aiocb r = { .aio_fildes = fd, .aio_buf = back, .aio_nbytes = sizeof back, .aio_offset = 4096 };
	CHECK(aio_read(&r) == 0);
	wait_one(&r);
	CHECK(aio_return(&r) == (ssize_t)sizeof back && !memcmp(back, data, sizeof data));

	/* signal notification carries SI_ASYNCIO and the value */
	int sig = SIGRTMIN + 3;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	sigprocmask(SIG_BLOCK, &set, 0);
	struct aiocb r2 = { .aio_fildes = fd, .aio_buf = back, .aio_nbytes = 10, .aio_offset = 0 };
	r2.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	r2.aio_sigevent.sigev_signo = sig;
	r2.aio_sigevent.sigev_value.sival_int = 42;
	CHECK(aio_read(&r2) == 0);
	siginfo_t si;
	CHECK(sigwaitinfo(&set, &si) == sig);
	CHECK(si.si_code == SI_ASYNCIO && si.si_value.sival_int == 42 && si.si_pid == getpid());
	CHECK(aio_error(&r2) == 0 && aio_return(&r2) == 10);

	/* thread notification */
	struct aiocb r3 = { .aio_fildes = fd, .aio_buf = back, .aio_nbytes = 10, .aio_offset = 0 };
	r3.aio_sigevent.sigev_notify = SIGEV_THREAD;
	r3.aio_sigevent.sigev_notify_function = on_done;
	r3.aio_sigevent.sigev_value.sival_int = 1;
	CHECK(aio_read(&r3) == 0);
	while (!__atomic_load_n(&cb_hits, __ATOMIC_SEQ_CST))
		sched_yield();

	/* lio_listio: wait mode, then no-wait with a completion signal */
	char b1[8], b2[8];
	struct aiocb l1 = { .aio_fildes = fd, .aio_lio_opcode = LIO_READ, .aio_buf = b1, .aio_nbytes = 8, .aio_offset = 4096 };
	struct aiocb l2 = { .aio_fildes = fd, .aio_lio_opcode = LIO_WRITE, .aio_buf = "ABCDEFGH", .aio_nbytes = 8, .aio_offset = 0 };
	struct aiocb l3 = { .aio_fildes = fd, .aio_lio_opcode = LIO_NOP };
	struct aiocb *list[] = { &l1, 0, &l2, &l3 };
	CHECK(lio_listio(LIO_WAIT, list, 4, 0) == 0);
	CHECK(aio_return(&l1) == 8 && !memcmp(b1, data, 8) && aio_return(&l2) == 8);
	l1.aio_buf = b2;
	l1.aio_offset = 0;
	struct aiocb *list2[] = { &l1 };
	struct sigevent ev = { .sigev_notify = SIGEV_SIGNAL, .sigev_signo = sig };
	ev.sigev_value.sival_int = 7;
	CHECK(lio_listio(LIO_NOWAIT, list2, 1, &ev) == 0);
	CHECK(sigwaitinfo(&set, &si) == sig && si.si_value.sival_int == 7);
	CHECK(aio_return(&l1) == 8 && !memcmp(b2, "ABCDEFGH", 8));
	/* a failing entry makes LIO_WAIT report EIO, and aio_error tells which */
	struct aiocb bad = { .aio_fildes = fd, .aio_lio_opcode = 99, .aio_buf = b1, .aio_nbytes = 1 };
	struct aiocb *list3[] = { &l1, &bad };
	l1.aio_lio_opcode = LIO_READ;
	errno = 0;
	CHECK(lio_listio(LIO_WAIT, list3, 2, 0) == -1 && errno == EIO);
	CHECK(aio_error(&l1) == 0 && aio_error(&bad) == EINVAL);

	/* a pipe: pread fails with ESPIPE so plain read is used; this one
	 * stays pending until data arrives */
	int p[2];
	CHECK(pipe(p) == 0);
	char pb[4];
	struct aiocb pr = { .aio_fildes = p[0], .aio_buf = pb, .aio_nbytes = 4 };
	CHECK(aio_read(&pr) == 0);
	const struct aiocb *pl[1] = { &pr };
	struct timespec ts = { 0, 20000000 };
	errno = 0;
	CHECK(aio_suspend(pl, 1, &ts) == -1 && errno == EAGAIN);
	CHECK(aio_error(&pr) == EINPROGRESS);
	CHECK(aio_return(&pr) == -1 && errno == EINVAL);
	CHECK(aio_cancel(p[0], &pr) == AIO_NOTCANCELED);
	CHECK(aio_cancel(p[0], 0) == AIO_NOTCANCELED);
	CHECK(write(p[1], "pipe", 4) == 4);
	wait_one(&pr);
	CHECK(aio_return(&pr) == 4 && !memcmp(pb, "pipe", 4));
	CHECK(aio_cancel(p[0], 0) == AIO_ALLDONE);
	CHECK(aio_cancel(p[0], &pr) == AIO_ALLDONE);

	/* errors are reported at submission */
	struct aiocb e1 = { .aio_fildes = p[1], .aio_buf = pb, .aio_nbytes = 1 };
	CHECK(aio_read(&e1) == -1 && errno == EBADF); /* write end */
	e1.aio_fildes = 9999;
	CHECK(aio_write(&e1) == -1 && errno == EBADF);
	CHECK(aio_fsync(12345, &s) == -1 && errno == EINVAL);
	CHECK(aio_cancel(9999, 0) == -1 && errno == EBADF);
	CHECK(aio_cancel(fd, &pr) == -1 && errno == EINVAL);
	struct aiocb e2 = { .aio_fildes = fd, .aio_buf = pb, .aio_nbytes = 1, .aio_offset = -1 };
	CHECK(aio_read(&e2) == -1 && errno == EINVAL);
	e2.aio_offset = 0;
	e2.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
	e2.aio_sigevent.sigev_signo = 200;
	CHECK(aio_read(&e2) == -1 && errno == EINVAL);
	CHECK(lio_listio(7, list2, 1, 0) == -1 && errno == EINVAL);

	close(p[0]);
	close(p[1]);
	close(fd);
	return t_done();
}
