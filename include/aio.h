#ifndef _AIO_H
#define _AIO_H
#include <features.h>
#include <bits/alltypes.h>
#include <signal.h>
#include <time.h>
__BEGIN_DECLS

struct aiocb {
	int aio_fildes;
	int aio_lio_opcode;
	int aio_reqprio;
	volatile void *aio_buf;
	size_t aio_nbytes;
	struct sigevent aio_sigevent;
	off_t aio_offset;
	/* private: completion state */
	volatile int __err;
	ssize_t __ret;
	char __reserved[32];
};
#define aiocb64 aiocb

#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

#define LIO_READ  0
#define LIO_WRITE 1
#define LIO_NOP   2

#define LIO_WAIT   0
#define LIO_NOWAIT 1

int aio_read(struct aiocb *);
int aio_write(struct aiocb *);
int aio_fsync(int, struct aiocb *);
int aio_error(const struct aiocb *);
ssize_t aio_return(struct aiocb *);
int aio_cancel(int, struct aiocb *);
int aio_suspend(const struct aiocb *const[], int, const struct timespec *);
int lio_listio(int, struct aiocb *__restrict const[__restrict], int, struct sigevent *__restrict);

__END_DECLS
#endif
