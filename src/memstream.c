/* Memory streams: fmemopen and open_memstream. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Deliver the buffered bytes and then the new ones through raw(); follows
 * the write-function contract in stdio_impl.h. */
static size_t buffered_write(FILE *f, const unsigned char *s, size_t l,
                             size_t (*raw)(void *, const unsigned char *, size_t))
{
	size_t pending = (size_t)(f->wpos - f->wbase);
	if (pending && raw(f->cookie, f->wbase, pending) < pending)
		goto fail;
	size_t n = l ? raw(f->cookie, s, l) : 0;
	if (n < l)
		goto fail;
	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;
	return n;
fail:
	f->wpos = f->wbase = f->wend = 0;
	f->flags |= F_ERR;
	return 0;
}

static off_t new_pos(off_t base, off_t off, off_t max)
{
	off_t p;
	if (__builtin_add_overflow(base, off, &p) || p < 0 || p > max) {
		errno = EINVAL;
		return -1;
	}
	return p;
}

/* ---- fmemopen ---- */

struct fmem {
	size_t pos, len, size;
	unsigned char *buf;
	int append, owned;
};

static size_t fmem_read(FILE *f, unsigned char *d, size_t l)
{
	struct fmem *c = f->cookie;
	size_t avail = c->pos < c->len ? c->len - c->pos : 0;
	if (l > avail)
		l = avail;
	if (!l) {
		f->flags |= F_EOF;
		return 0;
	}
	memcpy(d, c->buf + c->pos, l);
	c->pos += l;
	return l;
}

static size_t fmem_raw_write(void *cookie, const unsigned char *s, size_t l)
{
	struct fmem *c = cookie;
	if (c->append)
		c->pos = c->len;
	size_t room = c->size - c->pos;
	size_t n = l < room ? l : room;
	memcpy(c->buf + c->pos, s, n);
	c->pos += n;
	if (c->pos > c->len)
		c->len = c->pos;
	if (c->len < c->size)
		c->buf[c->len] = 0;
	if (n < l)
		errno = ENOSPC;
	return n;
}

static size_t fmem_write(FILE *f, const unsigned char *s, size_t l)
{
	return buffered_write(f, s, l, fmem_raw_write);
}

static off_t fmem_seek(FILE *f, off_t off, int whence)
{
	struct fmem *c = f->cookie;
	off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)c->pos : (off_t)c->len;
	off_t p = new_pos(base, off, (off_t)c->size);
	if (p >= 0)
		c->pos = (size_t)p;
	return p;
}

static int fmem_close(FILE *f)
{
	struct fmem *c = f->cookie;
	if (c->owned)
		free(c->buf);
	return 0;
}

FILE *fmemopen(void *__restrict buf, size_t size, const char *__restrict mode)
{
	if (!size || !mode || !strchr("rwa", *mode) || (!buf && !strchr(mode, '+'))) {
		errno = EINVAL;
		return 0;
	}
	struct {
		FILE f;
		struct fmem c;
		unsigned char b[UNGET + BUFSIZ];
	} *m = malloc(sizeof *m);
	if (!m)
		return 0;
	unsigned flags = 0;
	if (!strchr(mode, '+'))
		flags = *mode == 'r' ? F_NOWR : F_NORD;
	__file_init(&m->f, -1, flags, m->b, sizeof m->b);
	struct fmem *c = &m->c;
	memset(c, 0, sizeof *c);
	c->size = size;
	c->buf = buf;
	if (!buf) {
		c->buf = calloc(1, size);
		if (!c->buf) {
			free(m);
			return 0;
		}
		c->owned = 1;
	}
	if (*mode == 'r')
		c->len = size;
	else if (*mode == 'a')
		c->pos = c->len = strnlen((char *)c->buf, size);
	else
		c->buf[0] = 0;
	c->append = *mode == 'a';
	m->f.cookie = c;
	m->f.read_fn = __f_fn(fmem_read);
	m->f.write_fn = __f_fn(fmem_write);
	m->f.seek_fn = __f_fn(fmem_seek);
	m->f.close_fn = __f_fn(fmem_close);
	return __ofl_add(&m->f);
}

/* ---- open_memstream ---- */

struct mstream {
	char **bufp;
	size_t *sizep;
	size_t pos, len, space;
	char *buf;
};

static size_t ms_raw_write(void *cookie, const unsigned char *s, size_t l)
{
	struct mstream *c = cookie;
	size_t end;
	if (__builtin_add_overflow(c->pos, l, &end) || end >= SIZE_MAX / 2) {
		errno = ENOMEM;
		return 0;
	}
	if (end + 1 > c->space) {
		size_t sp = c->space * 2 > end + 1 ? c->space * 2 : end + 1;
		char *nb = realloc(c->buf, sp);
		if (!nb) {
			errno = ENOMEM;
			return 0;
		}
		c->buf = nb;
		c->space = sp;
		*c->bufp = nb;
	}
	if (c->pos > c->len) /* seeked past the end: fill the gap */
		memset(c->buf + c->len, 0, c->pos - c->len);
	memcpy(c->buf + c->pos, s, l);
	c->pos = end;
	if (end > c->len)
		c->len = end;
	c->buf[c->len] = 0;
	*c->sizep = c->pos < c->len ? c->pos : c->len;
	return l;
}

static size_t ms_write(FILE *f, const unsigned char *s, size_t l)
{
	return buffered_write(f, s, l, ms_raw_write);
}

static off_t ms_seek(FILE *f, off_t off, int whence)
{
	struct mstream *c = f->cookie;
	off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)c->pos : (off_t)c->len;
	off_t p = new_pos(base, off, SSIZE_MAX);
	if (p >= 0) {
		c->pos = (size_t)p;
		*c->sizep = c->pos < c->len ? c->pos : c->len;
	}
	return p;
}

static int ms_close(FILE *f)
{
	return 0;
}

FILE *open_memstream(char **bufp, size_t *sizep)
{
	if (!bufp || !sizep) {
		errno = EINVAL;
		return 0;
	}
	struct {
		FILE f;
		struct mstream c;
		unsigned char b[UNGET + BUFSIZ];
	} *m = malloc(sizeof *m);
	char *buf = malloc(1);
	if (!m || !buf) {
		free(m);
		free(buf);
		return 0;
	}
	__file_init(&m->f, -1, F_NORD, m->b, sizeof m->b);
	struct mstream *c = &m->c;
	memset(c, 0, sizeof *c);
	c->bufp = bufp;
	c->sizep = sizep;
	c->buf = buf;
	c->space = 1;
	buf[0] = 0;
	*bufp = buf;
	*sizep = 0;
	m->f.cookie = c;
	m->f.write_fn = __f_fn(ms_write);
	m->f.seek_fn = __f_fn(ms_seek);
	m->f.close_fn = __f_fn(ms_close);
	return __ofl_add(&m->f);
}
