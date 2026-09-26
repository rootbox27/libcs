/* stdio core: the standard streams, buffering, opening and closing,
 * flushing, positioning and locking. */
#include "stdio_impl.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

/* ---- standard streams ---- */

static unsigned char stdin_buf[UNGET + BUFSIZ];
static unsigned char stdout_buf[UNGET + BUFSIZ];

static FILE f_stdin = {
	.flags = F_PERM | F_NOWR,
	.buf = stdin_buf + UNGET,
	.buf_size = BUFSIZ,
	.fd = 0,
	.lbf = -1,
};
static FILE f_stdout = {
	.flags = F_PERM | F_NORD | F_TTYCHK,
	.buf = stdout_buf + UNGET,
	.buf_size = BUFSIZ,
	.fd = 1,
	.lbf = -1,
};
static FILE f_stderr = {
	.flags = F_PERM | F_NORD,
	.buf = f_stderr.sbuf + UNGET,
	.buf_size = 0,
	.fd = 2,
	.lbf = '\n',
};
FILE *const stdin = &f_stdin;
FILE *const stdout = &f_stdout;
FILE *const stderr = &f_stderr;

/* ---- open file list (streams from fopen and friends) ---- */

static FILE *ofl_head;
static volatile int ofl_lock;

hidden FILE *__ofl_add(FILE *f)
{
	LOCK(ofl_lock);
	f->prev = 0;
	f->next = ofl_head;
	if (ofl_head)
		ofl_head->prev = f;
	ofl_head = f;
	UNLOCK(ofl_lock);
	return f;
}

hidden void __ofl_remove(FILE *f)
{
	LOCK(ofl_lock);
	if (f->prev)
		f->prev->next = f->next;
	else if (ofl_head == f)
		ofl_head = f->next;
	if (f->next)
		f->next->prev = f->prev;
	f->prev = f->next = 0;
	UNLOCK(ofl_lock);
}

/* ---- locking: recursive, owned by thread id ---- */

hidden void __flock(FILE *f)
{
	int tid = __self()->tid;
	if (f->owner == tid) {
		if (f->lock_count == INT_MAX)
			__fatal("stdio lock count overflow");
		f->lock_count++;
		return;
	}
	__lock(&f->lock);
	f->owner = tid;
	f->lock_count = 1;
}

hidden void __funlock(FILE *f)
{
	if (--f->lock_count == 0) {
		f->owner = 0;
		__unlock(&f->lock);
	}
}

void flockfile(FILE *f) { __flock(f); }
void funlockfile(FILE *f) { __funlock(f); }

int ftrylockfile(FILE *f)
{
	int tid = __self()->tid;
	if (f->owner == tid) {
		if (f->lock_count == INT_MAX)
			return -1;
		f->lock_count++;
		return 0;
	}
	if (__sync_val_compare_and_swap(&f->lock, 0, 1) != 0)
		return -1;
	f->owner = tid;
	f->lock_count = 1;
	return 0;
}

/* ---- file descriptor operations ---- */

hidden size_t __stdio_read(FILE *f, unsigned char *buf, size_t len)
{
	long r = __sys(SYS_read, f->fd, buf, len);
	if (r > 0)
		return (size_t)r;
	if (r == 0) {
		f->flags |= F_EOF;
	} else {
		f->flags |= F_ERR;
		errno = (int)-r;
	}
	return 0;
}

hidden size_t __stdio_write(FILE *f, const unsigned char *buf, size_t len)
{
	struct iovec iov[2] = {
		{ f->wbase, (size_t)(f->wpos - f->wbase) },
		{ (void *)buf, len },
	};
	struct iovec *v = iov;
	int cnt = 2;
	size_t rem = iov[0].iov_len + len;
	if (!iov[0].iov_len) {
		v++;
		cnt--;
	}
	while (rem) {
		long r = __sys(SYS_writev, f->fd, v, cnt);
		if (r <= 0) {
			f->wpos = f->wbase = f->wend = 0;
			f->flags |= F_ERR;
			if (r < 0)
				errno = (int)-r;
			return cnt == 2 ? 0 : len - v[0].iov_len;
		}
		rem -= (size_t)r;
		while (cnt && (size_t)r >= v[0].iov_len) {
			r -= (long)v[0].iov_len;
			v++;
			cnt--;
		}
		if (cnt) {
			v[0].iov_base = (char *)v[0].iov_base + r;
			v[0].iov_len -= (size_t)r;
		}
	}
	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;
	return len;
}

hidden off_t __stdio_seek(FILE *f, off_t off, int whence)
{
	return sys(SYS_lseek, f->fd, off, whence);
}

hidden int __stdio_close(FILE *f)
{
	return close(f->fd);
}

/* ---- mode switching and buffer refill/flush ---- */

hidden int __toread(FILE *f)
{
	if (f->wpos != f->wbase)
		f_write(f, 0, 0);
	f->wpos = f->wbase = f->wend = 0;
	if (f->flags & F_NORD) {
		f->flags |= F_ERR;
		errno = EBADF;
		return EOF;
	}
	if (!f->rpos)
		f->rpos = f->rend = f->buf;
	return (f->flags & F_EOF) ? EOF : 0;
}

hidden int __towrite(FILE *f)
{
	if (f->flags & F_NOWR) {
		f->flags |= F_ERR;
		errno = EBADF;
		return EOF;
	}
	if (f->flags & F_TTYCHK) {
		f->flags &= ~F_TTYCHK;
		int e = errno;
		if (!(f->flags & F_SVB) && isatty(f->fd))
			f->lbf = '\n';
		errno = e;
	}
	/* Any unread input is discarded; C requires a positioning call
	 * between input and output anyway. */
	f->rpos = f->rend = 0;
	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;
	return 0;
}

hidden int __uflow(FILE *f)
{
	if (__toread(f))
		return EOF;
	/* Reading from the terminal: make any pending prompt visible. */
	if (f == &f_stdin && f_stdout.lbf >= 0 && f_stdout.wpos != f_stdout.wbase) {
		FLOCK(&f_stdout);
		__fflush_unlocked(&f_stdout);
		FUNLOCK(&f_stdout);
	}
	size_t n = f_read(f, f->buf, f->buf_size ? f->buf_size : 1);
	if (!n)
		return EOF;
	f->rpos = f->buf;
	f->rend = f->buf + n;
	return *f->rpos++;
}

hidden int __overflow(FILE *f, int ch)
{
	unsigned char c = (unsigned char)ch;
	if (!f->wend && __towrite(f))
		return EOF;
	if (f->wpos != f->wend && c != f->lbf) {
		*f->wpos++ = c;
		return c;
	}
	if (f_write(f, &c, 1) != 1)
		return EOF;
	return c;
}

hidden size_t __fwritex(const unsigned char *s, size_t l, FILE *f)
{
	size_t i = 0;
	if (!f->wend && __towrite(f))
		return 0;
	if (l > (size_t)(f->wend - f->wpos))
		return f_write(f, s, l);
	if (f->lbf >= 0) {
		/* Write through up to and including the last newline. */
		for (i = l; i && s[i - 1] != '\n'; i--) ;
		if (i) {
			size_t n = f_write(f, s, i);
			if (n < i)
				return n;
			s += i;
			l -= i;
		}
	}
	memcpy(f->wpos, s, l);
	f->wpos += l;
	return l + i;
}

/* ---- flushing ---- */

hidden int __fflush_unlocked(FILE *f)
{
	if (f->wpos != f->wbase) {
		f_write(f, 0, 0);
		if (!f->wpos)
			return EOF;
	}
	/* Give unread input back to the file so the descriptor position
	 * matches the stream position (fails harmlessly on pipes). */
	if (f->rpos != f->rend)
		f_seek(f, f->rpos - f->rend, SEEK_CUR);
	f->wpos = f->wbase = f->wend = 0;
	f->rpos = f->rend = 0;
	return 0;
}

static int flush_output(FILE *f)
{
	int r = 0;
	FLOCK(f);
	if (f->wpos != f->wbase)
		r = __fflush_unlocked(f);
	FUNLOCK(f);
	return r;
}

int fflush(FILE *f)
{
	if (f) {
		FLOCK(f);
		int r = __fflush_unlocked(f);
		FUNLOCK(f);
		return r;
	}
	int r = flush_output(&f_stdout) | flush_output(&f_stderr);
	LOCK(ofl_lock);
	for (FILE *p = ofl_head; p; p = p->next)
		r |= flush_output(p);
	UNLOCK(ofl_lock);
	return r;
}

/* Called from exit(). Streams are left locked so no other thread can
 * write into them after the final flush. */
static void exit_flush(FILE *f)
{
	FLOCK(f);
	if (f->wpos != f->wbase)
		f_write(f, 0, 0);
	if (f->rpos != f->rend)
		f_seek(f, f->rpos - f->rend, SEEK_CUR);
}

hidden void __stdio_exit(void)
{
	LOCK(ofl_lock);
	for (FILE *p = ofl_head; p; p = p->next)
		exit_flush(p);
	exit_flush(&f_stdin);
	exit_flush(&f_stdout);
	exit_flush(&f_stderr);
}

/* ---- opening and closing ---- */

hidden int __fmodeflags(const char *mode)
{
	int flags;
	if (strchr(mode, '+'))
		flags = O_RDWR;
	else if (*mode == 'r')
		flags = O_RDONLY;
	else
		flags = O_WRONLY;
	if (strchr(mode, 'x'))
		flags |= O_EXCL;
	if (strchr(mode, 'e'))
		flags |= O_CLOEXEC;
	if (*mode != 'r')
		flags |= O_CREAT;
	if (*mode == 'w')
		flags |= O_TRUNC;
	if (*mode == 'a')
		flags |= O_APPEND;
	return flags;
}

static int valid_mode(const char *mode)
{
	if (!mode || !strchr("rwa", *mode)) {
		errno = EINVAL;
		return 0;
	}
	return 1;
}

hidden void __file_init(FILE *f, int fd, unsigned flags, unsigned char *buf, size_t size)
{
	memset(f, 0, sizeof *f);
	f->flags = flags;
	f->fd = fd;
	f->lbf = -1;
	f->buf = buf + UNGET;
	f->buf_size = size - UNGET;
}

hidden FILE *__fdopen_flags(int fd, const char *mode)
{
	FILE *f = malloc(sizeof *f + UNGET + BUFSIZ);
	if (!f)
		return 0;
	unsigned flags = 0;
	if (!strchr(mode, '+'))
		flags = *mode == 'r' ? F_NOWR : F_NORD;
	if (*mode == 'a')
		flags |= F_APP;
	__file_init(f, fd, flags | F_TTYCHK, (unsigned char *)(f + 1), UNGET + BUFSIZ);
	return __ofl_add(f);
}

FILE *fdopen(int fd, const char *mode)
{
	if (!valid_mode(mode))
		return 0;
	int fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return 0;
	if (*mode == 'a' && !(fl & O_APPEND))
		fcntl(fd, F_SETFL, fl | O_APPEND);
	if (strchr(mode, 'e'))
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	return __fdopen_flags(fd, mode);
}

FILE *fopen(const char *__restrict path, const char *__restrict mode)
{
	if (!valid_mode(mode))
		return 0;
	int fd = open(path, __fmodeflags(mode), 0666);
	if (fd < 0)
		return 0;
	FILE *f = __fdopen_flags(fd, mode);
	if (!f) {
		int e = errno;
		close(fd);
		errno = e;
	}
	return f;
}

int fclose(FILE *f)
{
	FLOCK(f);
	int r = __fflush_unlocked(f);
	r |= f_close(f);
	FUNLOCK(f);
	if (f->flags & F_PERM) {
		/* The standard streams are never freed; mark them dead. */
		f->flags |= F_NORD | F_NOWR | F_ERR;
		f->fd = -1;
		return r;
	}
	__ofl_remove(f);
	f->fd = -1;
	f->read_fn = f->write_fn = f->seek_fn = f->close_fn = 0;
	free(f);
	return r;
}

FILE *freopen(const char *__restrict path, const char *__restrict mode, FILE *__restrict f)
{
	if (!valid_mode(mode)) {
		fclose(f);
		return 0;
	}
	FLOCK(f);
	__fflush_unlocked(f);
	int fl = __fmodeflags(mode);
	if (!path) {
		/* Change the mode of the existing descriptor. */
		if (fl & O_CLOEXEC)
			fcntl(f->fd, F_SETFD, FD_CLOEXEC);
		if (fcntl(f->fd, F_SETFL, fl & ~(O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC)) < 0)
			goto fail;
	} else {
		int fd = open(path, fl, 0666);
		if (fd < 0)
			goto fail;
		/* Keep the descriptor number: freopen(.., stdout) must still
		 * write to fd 1. */
		if (fd != f->fd) {
			int r = dup3(fd, f->fd, fl & O_CLOEXEC);
			close(fd);
			if (r < 0)
				goto fail;
		}
	}
	f->flags &= F_PERM | F_SVB;
	if (!strchr(mode, '+'))
		f->flags |= *mode == 'r' ? F_NOWR : F_NORD;
	if (*mode == 'a')
		f->flags |= F_APP;
	f->rpos = f->rend = f->wpos = f->wbase = f->wend = 0;
	FUNLOCK(f);
	return f;
fail:
	FUNLOCK(f);
	fclose(f);
	return 0;
}

/* ---- buffering control ---- */

int setvbuf(FILE *__restrict f, char *__restrict buf, int mode, size_t size)
{
	FLOCK(f);
	f->lbf = -1;
	if (mode == _IONBF) {
		f->buf = f->sbuf + UNGET;
		f->buf_size = 0;
	} else if (mode == _IOLBF || mode == _IOFBF) {
		if (buf && size >= UNGET + 1) {
			f->buf = (unsigned char *)buf + UNGET;
			f->buf_size = size - UNGET;
		}
		if (mode == _IOLBF && f->buf_size)
			f->lbf = '\n';
	} else {
		FUNLOCK(f);
		errno = EINVAL;
		return -1;
	}
	f->flags |= F_SVB;
	f->flags &= ~F_TTYCHK;
	FUNLOCK(f);
	return 0;
}

void setbuf(FILE *__restrict f, char *__restrict buf)
{
	setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setlinebuf(FILE *f)
{
	setvbuf(f, 0, _IOLBF, 0);
}

/* ---- positioning ---- */

hidden int __fseeko_unlocked(FILE *f, off_t off, int whence)
{
	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
		errno = EINVAL;
		return -1;
	}
	if (whence == SEEK_CUR && f->rend)
		off -= f->rend - f->rpos;
	if (f->wpos != f->wbase) {
		f_write(f, 0, 0);
		if (!f->wpos)
			return -1;
	}
	f->wpos = f->wbase = f->wend = 0;
	if (f_seek(f, off, whence) < 0)
		return -1;
	f->rpos = f->rend = 0;
	f->flags &= ~F_EOF;
	return 0;
}

int fseeko(FILE *f, off_t off, int whence)
{
	FLOCK(f);
	int r = __fseeko_unlocked(f, off, whence);
	FUNLOCK(f);
	return r;
}

int fseek(FILE *f, long off, int whence)
{
	return fseeko(f, off, whence);
}

hidden off_t __ftello_unlocked(FILE *f)
{
	int whence = (f->flags & F_APP) && f->wpos != f->wbase ? SEEK_END : SEEK_CUR;
	off_t pos = f_seek(f, 0, whence);
	if (pos < 0)
		return pos;
	if (f->rend)
		pos += f->rpos - f->rend;
	else if (f->wbase)
		pos += f->wpos - f->wbase;
	return pos;
}

off_t ftello(FILE *f)
{
	FLOCK(f);
	off_t r = __ftello_unlocked(f);
	FUNLOCK(f);
	return r;
}

long ftell(FILE *f)
{
	return ftello(f);
}

void rewind(FILE *f)
{
	FLOCK(f);
	__fseeko_unlocked(f, 0, SEEK_SET);
	f->flags &= ~F_ERR;
	FUNLOCK(f);
}

int fgetpos(FILE *__restrict f, fpos_t *__restrict pos)
{
	off_t r = ftello(f);
	if (r < 0)
		return -1;
	pos->__pos = r;
	return 0;
}

int fsetpos(FILE *f, const fpos_t *pos)
{
	return fseeko(f, pos->__pos, SEEK_SET);
}

/* ---- status ---- */

int feof(FILE *f)
{
	FLOCK(f);
	int r = !!(f->flags & F_EOF);
	FUNLOCK(f);
	return r;
}

int ferror(FILE *f)
{
	FLOCK(f);
	int r = !!(f->flags & F_ERR);
	FUNLOCK(f);
	return r;
}

void clearerr(FILE *f)
{
	FLOCK(f);
	f->flags &= ~(F_EOF | F_ERR);
	FUNLOCK(f);
}

int fileno(FILE *f)
{
	FLOCK(f);
	int fd = f->fd;
	FUNLOCK(f);
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	return fd;
}

size_t __fpending(FILE *f) { return f->wend ? (size_t)(f->wpos - f->wbase) : 0; }
int __freading(FILE *f) { return (f->flags & F_NOWR) || f->rend; }
int __fwriting(FILE *f) { return (f->flags & F_NORD) || f->wend; }
void __fpurge(FILE *f)
{
	f->wpos = f->wbase = f->wend = 0;
	f->rpos = f->rend = 0;
}

/* ---- temporary files ---- */

FILE *tmpfile(void)
{
	int fd = open(P_tmpdir, O_TMPFILE | O_RDWR | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) {
		/* No O_TMPFILE support: create a random name and unlink it. */
		char name[] = P_tmpdir "/tmpf_XXXXXXXXXXXX";
		static const char set[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
		for (int tries = 0; tries < 100 && fd < 0; tries++) {
			unsigned char r[12];
			__secure_random(r, sizeof r);
			for (int i = 0; i < 12; i++)
				name[sizeof name - 13 + i] = set[r[i] & 63];
			fd = open(name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
			if (fd < 0 && errno != EEXIST)
				return 0;
		}
		if (fd < 0)
			return 0;
		unlink(name);
	}
	FILE *f = __fdopen_flags(fd, "w+");
	if (!f)
		close(fd);
	return f;
}

/* tmpnam can only return a name, which someone else may create before the
 * caller does; mkstemp or tmpfile avoid that. The names here are hard to
 * predict (72 random bits) and checked not to exist yet. */
char *tmpnam(char *buf)
{
	static __thread char internal[L_tmpnam];
	char name[] = P_tmpdir "/tmpnam_XXXXXXXXXXXX";
	_Static_assert(sizeof name <= L_tmpnam, "L_tmpnam");
	static const char set[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
	for (int tries = 0; tries < TMP_MAX; tries++) {
		unsigned char r[12];
		__secure_random(r, sizeof r);
		for (int i = 0; i < 12; i++)
			name[sizeof name - 13 + i] = set[r[i] & 63];
		struct stat st;
		if (lstat(name, &st) < 0 && errno == ENOENT) {
			if (!buf)
				buf = internal;
			memcpy(buf, name, sizeof name);
			return buf;
		}
	}
	return 0;
}
