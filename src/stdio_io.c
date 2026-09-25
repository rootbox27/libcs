/* stdio character, line and block I/O. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int getc_unlocked(FILE *f) { return getc_unlocked_(f); }
int fgetc_unlocked(FILE *f) { return getc_unlocked_(f); }
int getchar_unlocked(void) { return getc_unlocked_(stdin); }
int putc_unlocked(int c, FILE *f) { return putc_unlocked_(c, f); }
int fputc_unlocked(int c, FILE *f) { return putc_unlocked_(c, f); }
int putchar_unlocked(int c) { return putc_unlocked_(c, stdout); }

int fgetc(FILE *f)
{
	FLOCK(f);
	int c = getc_unlocked_(f);
	FUNLOCK(f);
	return c;
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int fputc(int c, FILE *f)
{
	FLOCK(f);
	c = putc_unlocked_(c, f);
	FUNLOCK(f);
	return c;
}
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int ungetc(int c, FILE *f)
{
	if (c == EOF)
		return EOF;
	FLOCK(f);
	if (!f->rpos)
		__toread(f);
	if (!f->rpos || f->rpos <= f->buf - UNGET) {
		FUNLOCK(f);
		return EOF;
	}
	*--f->rpos = (unsigned char)c;
	f->flags &= ~F_EOF;
	FUNLOCK(f);
	return (unsigned char)c;
}

static int mul_ok(size_t a, size_t b, size_t *r)
{
	if (__builtin_mul_overflow(a, b, r)) {
		errno = EOVERFLOW;
		return 0;
	}
	return 1;
}

size_t fwrite(const void *__restrict src, size_t size, size_t nmemb, FILE *__restrict f)
{
	size_t l;
	if (!mul_ok(size, nmemb, &l) || !l)
		return 0;
	FLOCK(f);
	size_t k = __fwritex(src, l, f);
	FUNLOCK(f);
	return k == l ? nmemb : k / size;
}

size_t fread(void *__restrict dst, size_t size, size_t nmemb, FILE *__restrict f)
{
	size_t len;
	if (!mul_ok(size, nmemb, &len) || !len)
		return 0;
	unsigned char *d = dst;
	size_t l = len;
	FLOCK(f);
	while (l) {
		if (f->rpos != f->rend) {
			size_t k = (size_t)(f->rend - f->rpos);
			if (k > l)
				k = l;
			memcpy(d, f->rpos, k);
			f->rpos += k;
			d += k;
			l -= k;
			continue;
		}
		if (__toread(f))
			break;
		if (l >= f->buf_size) {
			/* Large reads bypass the buffer. */
			size_t k = f_read(f, d, l);
			if (!k)
				break;
			d += k;
			l -= k;
		} else {
			size_t k = f_read(f, f->buf, f->buf_size);
			if (!k)
				break;
			f->rpos = f->buf;
			f->rend = f->buf + k;
		}
	}
	FUNLOCK(f);
	return (len - l) / size;
}

char *fgets(char *__restrict s, int n, FILE *__restrict f)
{
	if (n <= 0) {
		errno = EINVAL;
		return 0;
	}
	char *p = s;
	size_t room = (size_t)n - 1;
	int err = 0;
	FLOCK(f);
	while (room) {
		if (f->rpos != f->rend) {
			size_t k = (size_t)(f->rend - f->rpos);
			unsigned char *z = memchr(f->rpos, '\n', k);
			if (z)
				k = (size_t)(z - f->rpos) + 1;
			if (k > room)
				k = room;
			memcpy(p, f->rpos, k);
			f->rpos += k;
			p += k;
			room -= k;
			if (p[-1] == '\n')
				break;
			continue;
		}
		int c = __uflow(f);
		if (c == EOF) {
			err = !!(f->flags & F_ERR);
			break;
		}
		*p++ = (char)c;
		room--;
		if (c == '\n')
			break;
	}
	FUNLOCK(f);
	if (err || (p == s && n > 1))
		return 0;
	*p = 0;
	return s;
}

int fputs(const char *__restrict s, FILE *__restrict f)
{
	size_t l = strlen(s);
	FLOCK(f);
	int r = __fwritex((const unsigned char *)s, l, f) == l ? 0 : EOF;
	FUNLOCK(f);
	return r;
}

int puts(const char *s)
{
	FILE *f = stdout;
	FLOCK(f);
	size_t l = strlen(s);
	int r = -(__fwritex((const unsigned char *)s, l, f) != l || putc_unlocked_('\n', f) == EOF);
	FUNLOCK(f);
	return r;
}

ssize_t getdelim(char **__restrict line, size_t *__restrict size, int delim, FILE *__restrict f)
{
	if (!line || !size) {
		errno = EINVAL;
		return -1;
	}
	if (!*line)
		*size = 0;
	size_t i = 0;
	FLOCK(f);
	for (;;) {
		unsigned char *z = 0;
		size_t k = 0;
		if (f->rpos != f->rend) {
			k = (size_t)(f->rend - f->rpos);
			z = memchr(f->rpos, delim, k);
			if (z)
				k = (size_t)(z - f->rpos) + 1;
		}
		/* room for k bytes plus the terminator (and one more byte for
		 * the single-character path below) */
		if (i + k + 2 > *size || !*line) {
			size_t m = i + k + 2;
			if (m < i || m > SSIZE_MAX) {
				errno = EOVERFLOW;
				goto fail;
			}
			if (m < SSIZE_MAX / 2)
				m += m / 2 + 64;
			char *t = realloc(*line, m);
			if (!t) {
				errno = ENOMEM;
				goto fail;
			}
			*line = t;
			*size = m;
		}
		if (k) {
			memcpy(*line + i, f->rpos, k);
			f->rpos += k;
			i += k;
		}
		if (z)
			break;
		int c = __uflow(f);
		if (c == EOF) {
			if (!i || (f->flags & F_ERR))
				goto fail_eof;
			break;
		}
		(*line)[i++] = (char)c;
		if (c == delim)
			break;
	}
	(*line)[i] = 0;
	FUNLOCK(f);
	return (ssize_t)i;
fail:
	f->flags |= F_ERR;
fail_eof:
	if (*line && *size)
		(*line)[i < *size ? i : 0] = 0;
	FUNLOCK(f);
	return -1;
}

ssize_t getline(char **__restrict line, size_t *__restrict size, FILE *__restrict f)
{
	return getdelim(line, size, '\n', f);
}

void perror(const char *msg)
{
	FILE *f = stderr;
	const char *e = strerror(errno);
	FLOCK(f);
	if (msg && *msg) {
		fputs(msg, f);
		fputs(": ", f);
	}
	fputs(e, f);
	putc_unlocked_('\n', f);
	FUNLOCK(f);
}

/* ---- fortify entry points ---- */

char *__fgets_chk(char *s, size_t slen, int n, FILE *f)
{
	if (n > 0 && (size_t)n > slen)
		__chk_fail();
	return fgets(s, n, f);
}

size_t __fread_chk(void *p, size_t plen, size_t size, size_t n, FILE *f)
{
	size_t t;
	if (__builtin_mul_overflow(size, n, &t) || t > plen)
		__chk_fail();
	return fread(p, size, n, f);
}
