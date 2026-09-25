/* Formatted input: the scanf family.
 *
 * Supports all C11 conversions and length modifiers, assignment
 * suppression, POSIX %n$ positional arguments and the POSIX 'm'
 * (allocate) modifier. Numbers consume the longest prefix of a valid
 * number (at most one character is pushed back) and, like glibc, convert
 * the longest valid number within what was consumed: "1e" reads as 1 and
 * "0x" as 0 rather than failing. */
#include "stdio_impl.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct in {
	FILE *f;
	size_t cnt;     /* characters consumed */
};

static int get(struct in *in)
{
	int c = getc_unlocked_(in->f);
	if (c != EOF)
		in->cnt++;
	return c;
}

/* Push back the character just read. It is still in the buffer, so
 * moving the pointer back suffices (and never writes, which matters for
 * sscanf on read-only strings). */
static void unget(struct in *in, int c)
{
	if (c != EOF) {
		in->f->rpos--;
		in->cnt--;
	}
}

/* Growable collection buffer for a numeric field. */
struct cbuf {
	char *p;
	size_t n, cap;
	char small[128];
	int oom;
};

static void cb_init(struct cbuf *b)
{
	b->p = b->small;
	b->n = 0;
	b->cap = sizeof b->small;
	b->oom = 0;
}

static void cb_put(struct cbuf *b, int c)
{
	if (b->n + 1 >= b->cap) {
		size_t nc = b->cap * 2;
		char *np = b->p == b->small ? malloc(nc) : realloc(b->p, nc);
		if (!np) {
			b->oom = 1;
			return;
		}
		if (b->p == b->small)
			memcpy(np, b->small, b->n);
		b->p = np;
		b->cap = nc;
	}
	b->p[b->n++] = (char)c;
	b->p[b->n] = 0;
}

static void cb_free(struct cbuf *b)
{
	if (b->p != b->small)
		free(b->p);
}

static int hexdig(int c)
{
	return (unsigned)c - '0' < 10 || (unsigned)(c | 32) - 'a' < 6;
}

/* Collect an integer in the given base (0: C prefixes). Returns the base
 * used, or -1 on matching failure. Leaves the terminating char unread. */
static int scan_int(struct in *in, struct cbuf *b, size_t w, int base)
{
	int c = get(in);
	if ((c == '+' || c == '-') && w) {
		cb_put(b, c);
		w--;
		c = get(in);
	}
	int digits = 0;
	if ((base == 0 || base == 16) && c == '0' && w) {
		cb_put(b, c);
		w--;
		digits = 1;
		c = get(in);
		if ((c | 32) == 'x' && w) {
			cb_put(b, c);
			w--;
			c = get(in);
			base = 16;
			digits = 0;
			/* "0x" without digits converts as 0 (glibc) */
			if (!(w && hexdig(c))) {
				unget(in, c);
				return 16;
			}
		} else if (base == 0) {
			base = 8;
		}
	}
	if (!base)
		base = 10;
	for (; w; w--) {
		int d = (unsigned)c - '0' < 10 ? c - '0' : (unsigned)(c | 32) - 'a' < 26 ? (c | 32) - 'a' + 10 : 99;
		if (d >= base)
			break;
		cb_put(b, c);
		digits = 1;
		c = get(in);
	}
	unget(in, c);
	return digits ? base : -1;
}

/* Case-insensitively match the rest of `word` (first char already
 * matched). Returns 1 if matched fully, 0 if not at all (nothing extra
 * consumed), -1 if a partial match consumed characters. */
static int match_word(struct in *in, struct cbuf *b, size_t *w, const char *word)
{
	for (size_t i = 0; word[i]; i++) {
		int c = *w ? get(in) : EOF;
		if (c == EOF || (c | 32) != word[i]) {
			if (*w)
				unget(in, c);
			return i ? -1 : 0;
		}
		cb_put(b, c);
		(*w)--;
	}
	return 1;
}

/* Collect a floating-point number. Returns 0 on success, -1 on matching
 * failure. */
static int scan_float(struct in *in, struct cbuf *b, size_t w)
{
	int c = get(in);
	if ((c == '+' || c == '-') && w) {
		cb_put(b, c);
		w--;
		c = get(in);
	}
	if (w && (c | 32) == 'i') {
		cb_put(b, c);
		w--;
		if (match_word(in, b, &w, "nf") != 1)
			return -1;
		/* "inf" matched; "infinity" is optional but all or nothing */
		c = w ? get(in) : EOF;
		if ((c | 32) != 'i') {
			if (w)
				unget(in, c);
			return 0;
		}
		cb_put(b, c);
		w--;
		return match_word(in, b, &w, "nity") == 1 ? 0 : -1;
	}
	if (w && (c | 32) == 'n') {
		cb_put(b, c);
		w--;
		if (match_word(in, b, &w, "an") != 1)
			return -1;
		c = w ? get(in) : EOF;
		if (c != '(') {
			if (w)
				unget(in, c);
			return 0;
		}
		cb_put(b, c);
		w--;
		for (;;) {
			c = w ? get(in) : EOF;
			if (c == ')' ) {
				cb_put(b, c);
				return 0;
			}
			if (c == EOF || !(isalnum(c) || c == '_')) {
				unget(in, c);
				return 0; /* converts as plain "nan" */
			}
			cb_put(b, c);
			w--;
		}
	}
	int hex = 0, digits = 0;
	if (c == '0' && w) {
		cb_put(b, c);
		w--;
		digits = 1;
		c = get(in);
		if ((c | 32) == 'x' && w) {
			cb_put(b, c);
			w--;
			c = get(in);
			hex = 1;
			digits = 0;
		}
	}
	int dot = 0;
	for (; w; w--) {
		if (c == '.' && !dot) {
			dot = 1;
		} else if (hex ? hexdig(c) : (unsigned)c - '0' < 10) {
			digits = 1;
		} else {
			break;
		}
		cb_put(b, c);
		c = get(in);
	}
	if (!digits) {
		unget(in, c);
		/* glibc: "0x" cut short by the field width converts as 0, but
		 * "0x" followed by a non-digit is a matching failure */
		return hex && !w ? 0 : -1;
	}
	if (w && (c | 32) == (hex ? 'p' : 'e')) {
		/* exponent: once started it must be complete */
		cb_put(b, c);
		w--;
		c = w ? get(in) : EOF;
		if ((c == '+' || c == '-') && w) {
			cb_put(b, c);
			w--;
			c = w ? get(in) : EOF;
		}
		for (; w && (unsigned)c - '0' < 10; w--) {
			cb_put(b, c);
			c = get(in);
		}
		unget(in, c);
		return 0; /* an incomplete exponent is ignored (glibc) */
	}
	unget(in, c);
	return 0;
}

/* Storage for %s, %c and %[ results, including the 'm' and 'l' forms. */
struct sdest {
	char *s;          /* narrow destination (or allocated buffer) */
	wchar_t *ws;      /* wide destination */
	size_t n, cap;    /* elements written, allocated capacity */
	void **mptr;      /* 'm': where to store the allocation */
	int wide, alloc, fail;
	mbstate_t st;
};

static void sd_put(struct sdest *d, wchar_t c)
{
	if (d->fail)
		return;
	if (d->alloc && d->n + 1 >= d->cap) {
		size_t nc = d->cap ? d->cap * 2 : 32;
		void *np = reallocarray(d->wide ? (void *)d->ws : (void *)d->s, nc, d->wide ? sizeof(wchar_t) : 1);
		if (!np) {
			d->fail = 1;
			return;
		}
		if (d->wide)
			d->ws = np;
		else
			d->s = np;
		d->cap = nc;
	}
	if (d->wide) {
		if (d->ws)
			d->ws[d->n] = c;
	} else if (d->s) {
		d->s[d->n] = (char)c;
	}
	d->n++;
}

/* Feed one input byte; wide destinations decode UTF-8. */
static int sd_byte(struct sdest *d, int c)
{
	if (!d->wide) {
		sd_put(d, (wchar_t)c);
		return 0;
	}
	char ch = (char)c;
	wchar_t wc;
	size_t r = mbrtowc(&wc, &ch, 1, &d->st);
	if (r == (size_t)-1)
		return -1;
	if (r != (size_t)-2)
		sd_put(d, wc);
	return 0;
}

static void *arg_n(va_list ap, unsigned n)
{
	va_list ap2;
	va_copy(ap2, ap);
	void *p = 0;
	for (unsigned i = 0; i < n; i++)
		p = va_arg(ap2, void *);
	va_end(ap2);
	return p;
}

enum { S_HH = -2, S_H = -1, S_INT = 0, S_L = 1, S_LL = 2, S_BIGL = 3 };

static void store_int(void *dest, int size, uintmax_t v)
{
	if (!dest)
		return;
	switch (size) {
	case S_HH: *(char *)dest = (char)v; break;
	case S_H: *(short *)dest = (short)v; break;
	case S_INT: *(int *)dest = (int)v; break;
	case S_L: *(long *)dest = (long)v; break;
	case S_LL: case S_BIGL: *(long long *)dest = (long long)v; break;
	}
}

int vfscanf(FILE *__restrict f, const char *__restrict fmt, va_list ap)
{
	struct in in = { f, 0 };
	int matches = 0, positional = 0;
	va_list seq;
	va_copy(seq, ap);
	FLOCK(f);
	if (!f->rpos)
		__toread(f);
	if (!f->rpos)
		goto input_fail;

	for (const unsigned char *p = (const unsigned char *)fmt; *p; p++) {
		if (isspace(*p)) {
			while (isspace(p[1]))
				p++;
			int c;
			while (isspace(c = get(&in))) ;
			unget(&in, c);
			continue;
		}
		if (*p != '%' || p[1] == '%') {
			int c;
			if (*p == '%') {
				p++;
				while (isspace(c = get(&in))) ;
			} else {
				c = get(&in);
			}
			if (c != *p) {
				unget(&in, c);
				if (c == EOF)
					goto input_fail;
				goto done;
			}
			continue;
		}

		/* conversion specification */
		p++;
		void *dest = 0;
		int suppress = 0;
		if (*p == '*') {
			suppress = 1;
			p++;
		}
		if (!suppress && (unsigned)*p - '0' < 10) {
			const unsigned char *q = p;
			unsigned n = 0;
			while ((unsigned)*q - '0' < 10 && n < 10000)
				n = n * 10 + (*q++ - '0');
			if (*q == '$') {
				if (!n || positional < 0)
					goto inval;
				positional = 1;
				dest = arg_n(ap, n);
				p = q + 1;
			}
		}
		size_t width = 0;
		int have_width = 0;
		while ((unsigned)*p - '0' < 10) {
			have_width = 1;
			size_t d = (size_t)(*p++ - '0');
			width = width > (SIZE_MAX - d) / 10 ? SIZE_MAX : width * 10 + d;
		}
		if (have_width && !width)
			goto inval;
		int alloc = 0;
		if (*p == 'm') {
			alloc = 1;
			p++;
		}
		int size = S_INT;
		switch (*p) {
		case 'h': p++; if (*p == 'h') { p++; size = S_HH; } else size = S_H; break;
		case 'l': p++; if (*p == 'l') { p++; size = S_LL; } else size = S_L; break;
		case 'q': p++; size = S_LL; break;
		case 'L': p++; size = S_BIGL; break;
		case 'j': case 'z': case 't': p++; size = S_L; break;
		}
		int t = *p;
		if (!t)
			goto inval;
		if (!suppress && !dest && t != '%') {
			if (positional > 0)
				goto inval;
			positional = -1;
			dest = va_arg(seq, void *);
		}
		if (alloc && t != 's' && t != 'c' && t != '[')
			goto inval;
		if (suppress)
			dest = 0;

		if (t != '[' && t != 'c' && t != 'n' && t != 'C') {
			int c;
			while (isspace(c = get(&in))) ;
			unget(&in, c);
			if (c == EOF)
				goto input_fail;
		}

		if (t == 'n') {
			store_int(dest, size, in.cnt);
			continue;
		}

		if (t == 'c' || t == 'C' || t == 's' || t == 'S' || t == '[') {
			int wide = size == S_L || t == 'C' || t == 'S';
			unsigned char set[256];
			if (t == '[') {
				p++;
				int invert = 0;
				if (*p == '^') {
					invert = 1;
					p++;
				}
				memset(set, invert, sizeof set);
				if (*p == ']') {
					set[']'] = !invert;
					p++;
				}
				for (; *p != ']'; p++) {
					if (!*p)
						goto inval;
					if (*p == '-' && p[1] != ']' && p[-1] != '[' && p[-1] != '^') {
						for (int ch = p[-1]; ch <= p[1]; ch++)
							set[ch] = !invert;
						p++;
						set[*p] = !invert;
						continue;
					}
					set[*p] = !invert;
				}
			} else if (t == 's' || t == 'S') {
				memset(set, 1, sizeof set);
				for (int ch = 0; ch < 256; ch++)
					if (isspace(ch))
						set[ch] = 0;
			}
			int is_c = t == 'c' || t == 'C';
			if (!have_width)
				width = is_c ? 1 : SIZE_MAX;

			struct sdest d = { 0 };
			d.wide = wide;
			d.alloc = alloc && dest;
			if (d.alloc) {
				d.mptr = dest;
			} else if (wide) {
				d.ws = dest;
			} else {
				d.s = dest;
			}
			/* The width counts characters: bytes, or for the wide
			 * forms whole multibyte characters. */
			size_t got = 0;
			int c = 0;
			while (got < width) {
				c = get(&in);
				if (c == EOF)
					break;
				if (!is_c && !set[c]) {
					unget(&in, c);
					break;
				}
				if (sd_byte(&d, c) < 0) {
					errno = EILSEQ;
					goto fail_alloc;
				}
				if (mbsinit(&d.st))
					got++;
			}
			if (!got && !mbsinit(&d.st))
				goto fail_alloc;
			if (!got) {
				if (c == EOF)
					goto input_fail_alloc;
				goto fail_alloc;
			}
			/* a short %c field at end of input is accepted (glibc) */
			if (!mbsinit(&d.st))
				goto fail_alloc; /* truncated multibyte character */
			if (!is_c)
				sd_put(&d, 0);
			if (d.fail) {
				errno = ENOMEM;
				goto fail_alloc;
			}
			if (d.alloc)
				*d.mptr = wide ? (void *)d.ws : (void *)d.s;
			if (dest)
				matches++;
			continue;
		fail_alloc:
			if (d.alloc)
				free(wide ? (void *)d.ws : (void *)d.s);
			goto done;
		input_fail_alloc:
			if (d.alloc)
				free(wide ? (void *)d.ws : (void *)d.s);
			goto input_fail;
		}

		if (!have_width)
			width = SIZE_MAX;
		struct cbuf b;
		cb_init(&b);
		switch (t) {
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
			int base = t == 'd' || t == 'u' ? 10 : t == 'o' ? 8 : t == 'i' ? 0 : 16;
			base = scan_int(&in, &b, width, base);
			if (base < 0 || b.oom) {
				cb_free(&b);
				goto done;
			}
			uintmax_t v;
			if (t == 'd' || t == 'i')
				v = (uintmax_t)strtoimax(b.p, 0, base);
			else
				v = strtoumax(b.p, 0, base);
			cb_free(&b);
			if (t == 'p') {
				if (dest)
					*(void **)dest = (void *)(uintptr_t)v;
			} else {
				store_int(dest, size, v);
			}
			break;
		}
		case 'a': case 'A': case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
			if (scan_float(&in, &b, width) < 0 || b.oom) {
				cb_free(&b);
				goto done;
			}
			if (dest) {
				if (size == S_BIGL)
					*(long double *)dest = strtold(b.p, 0);
				else if (size == S_L)
					*(double *)dest = strtod(b.p, 0);
				else
					*(float *)dest = strtof(b.p, 0);
			}
			cb_free(&b);
			break;
		}
		default:
			goto inval;
		}
		if (dest)
			matches++;
	}
done:
	FUNLOCK(f);
	va_end(seq);
	return matches;
input_fail:
	FUNLOCK(f);
	va_end(seq);
	return matches ? matches : EOF;
inval:
	errno = EINVAL;
	FUNLOCK(f);
	va_end(seq);
	return matches ? matches : EOF;
}

int fscanf(FILE *__restrict f, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfscanf(f, fmt, ap);
	va_end(ap);
	return r;
}

int vscanf(const char *__restrict fmt, va_list ap)
{
	return vfscanf(stdin, fmt, ap);
}

int scanf(const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfscanf(stdin, fmt, ap);
	va_end(ap);
	return r;
}

static size_t str_read(FILE *f, unsigned char *buf, size_t len)
{
	f->flags |= F_EOF;
	return 0;
}

int vsscanf(const char *__restrict s, const char *__restrict fmt, va_list ap)
{
	/* The string itself is the read buffer; nothing is copied and it is
	 * never written (pushback only moves rpos). */
	FILE f;
	__file_init(&f, -1, F_NOWR, (unsigned char *)(uintptr_t)s, UNGET);
	f.read_fn = __f_fn(str_read);
	f.rpos = (unsigned char *)(uintptr_t)s;
	f.rend = f.rpos + strlen(s);
	f.buf = f.rpos;
	f.buf_size = 0;
	return vfscanf(&f, fmt, ap);
}

int sscanf(const char *__restrict s, const char *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsscanf(s, fmt, ap);
	va_end(ap);
	return r;
}
