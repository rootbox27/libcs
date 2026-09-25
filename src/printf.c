/* printf family wrappers, string formatting and fortify entry points. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int printf(const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}

int vprintf(const char *__restrict fmt, va_list ap)
{
	return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *__restrict f, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- to a string ---- */

struct sn {
	char *s;
	size_t n; /* room left, excluding the terminator */
};

static size_t sn_append(struct sn *c, const unsigned char *s, size_t l)
{
	size_t k = l < c->n ? l : c->n;
	memcpy(c->s, s, k);
	c->s += k;
	c->n -= k;
	return k;
}

static size_t sn_write(FILE *f, const unsigned char *s, size_t l)
{
	struct sn *c = f->cookie;
	sn_append(c, f->wbase, (size_t)(f->wpos - f->wbase));
	sn_append(c, s, l);
	*c->s = 0;
	f->wpos = f->wbase = f->buf;
	f->wend = f->buf + f->buf_size;
	return l; /* excess is discarded but still counted */
}

int vsnprintf(char *__restrict s, size_t n, const char *__restrict fmt, va_list ap)
{
	unsigned char buf[UNGET + 256];
	char dummy;
	struct sn c = { n ? s : &dummy, n ? n - 1 : 0 };
	FILE f;
	__file_init(&f, -1, F_NORD, buf, sizeof buf);
	f.write_fn = __f_fn(sn_write);
	f.cookie = &c;
	*c.s = 0;
	FLOCK(&f);
	int r = __vfprintf_unlocked(&f, fmt, ap);
	sn_write(&f, 0, 0);
	FUNLOCK(&f);
	return r;
}

int snprintf(char *__restrict s, size_t n, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}

int vsprintf(char *__restrict s, const char *__restrict fmt, va_list ap)
{
	return vsnprintf(s, SIZE_MAX, fmt, ap);
}

int sprintf(char *__restrict s, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsprintf(s, fmt, ap);
	va_end(ap);
	return r;
}

int vasprintf(char **s, const char *fmt, va_list ap)
{
	va_list ap2;
	va_copy(ap2, ap);
	int l = vsnprintf(0, 0, fmt, ap2);
	va_end(ap2);
	*s = 0;
	if (l < 0)
		return -1;
	char *p = malloc((size_t)l + 1);
	if (!p)
		return -1;
	int r = vsnprintf(p, (size_t)l + 1, fmt, ap);
	if (r != l) {
		free(p);
		return -1;
	}
	*s = p;
	return l;
}

int asprintf(char **s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vasprintf(s, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- to a file descriptor ---- */

int vdprintf(int fd, const char *__restrict fmt, va_list ap)
{
	unsigned char buf[UNGET + 1];
	FILE f;
	__file_init(&f, fd, F_NORD, buf, UNGET); /* unbuffered */
	FLOCK(&f);
	int r = __vfprintf_unlocked(&f, fmt, ap);
	FUNLOCK(&f);
	return r;
}

int dprintf(int fd, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vdprintf(fd, fmt, ap);
	va_end(ap);
	return r;
}

/* ---- fortify entry points ---- */

int __vsnprintf_chk(char *s, size_t n, int flag, size_t slen, const char *fmt, va_list ap)
{
	if (n > slen)
		__chk_fail();
	return vsnprintf(s, n, fmt, ap);
}

int __snprintf_chk(char *s, size_t n, int flag, size_t slen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = __vsnprintf_chk(s, n, flag, slen, fmt, ap);
	va_end(ap);
	return r;
}

int __vsprintf_chk(char *s, int flag, size_t slen, const char *fmt, va_list ap)
{
	if (!slen)
		__chk_fail();
	int r = vsnprintf(s, slen, fmt, ap);
	if (r >= 0 && (size_t)r >= slen)
		__chk_fail();
	return r;
}

int __sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = __vsprintf_chk(s, flag, slen, fmt, ap);
	va_end(ap);
	return r;
}
