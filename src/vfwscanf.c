/* Formatted wide input: the wscanf family.
 *
 * The same conversions and rules as scanf (see vfscanf.c): C11
 * conversions and length modifiers, assignment suppression, %n$
 * positional arguments, the 'm' modifier, and glibc's treatment of
 * partial numbers ("1e" reads as 1, "0x" as 0). Input is wide characters
 * from a stream (decoded from UTF-8) or a wide string, read with one
 * character of lookahead. Numbers are collected as ASCII and converted by
 * the byte functions, so they round exactly as scanf's do.
 *
 * %c, %s and %[ without 'l' store the characters as multibyte (UTF-8)
 * strings; with 'l' as wide strings. Field widths count wide characters. */
#include "stdio_impl.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

struct in {
	FILE *f;
	const wchar_t *s;
	size_t cnt;     /* wide characters consumed */
	wint_t pend;    /* character read and given back */
	int has_pend;
	int err;        /* a stream read failed (or held a bad sequence) */
};

static wint_t get(struct in *in)
{
	wint_t c;
	if (in->has_pend) {
		in->has_pend = 0;
		c = in->pend;
	} else if (in->f) {
		c = __fgetwc_unlocked(in->f);
		if (c == WEOF && (in->f->flags & F_ERR))
			in->err = 1;
	} else {
		c = *in->s ? (wint_t)*in->s++ : WEOF;
	}
	if (c != WEOF)
		in->cnt++;
	return c;
}

static void unget(struct in *in, wint_t c)
{
	if (c != WEOF) {
		in->pend = c;
		in->has_pend = 1;
		in->cnt--;
	}
}

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

static void cb_put(struct cbuf *b, wint_t c)
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
	b->p[b->n++] = (char)c; /* only ASCII is ever collected */
	b->p[b->n] = 0;
}

static void cb_free(struct cbuf *b)
{
	if (b->p != b->small)
		free(b->p);
}

static int dig(wint_t c)
{
	return c - '0' < 10;
}

static int hexdig(wint_t c)
{
	return dig(c) || (c < 0x80 && (c | 32) - 'a' < 6);
}

static int lower(wint_t c)
{
	return c < 0x80 ? (int)(c | 32) : -1;
}

static int scan_int(struct in *in, struct cbuf *b, size_t w, int base)
{
	wint_t c = get(in);
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
		if (lower(c) == 'x' && w) {
			cb_put(b, c);
			w--;
			c = get(in);
			base = 16;
			digits = 0;
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
		int d = dig(c) ? (int)(c - '0') : (lower(c) >= 'a' && lower(c) <= 'z') ? lower(c) - 'a' + 10 : 99;
		if (d >= base)
			break;
		cb_put(b, c);
		digits = 1;
		c = get(in);
	}
	unget(in, c);
	return digits ? base : -1;
}

static int match_word(struct in *in, struct cbuf *b, size_t *w, const char *word)
{
	for (size_t i = 0; word[i]; i++) {
		wint_t c = *w ? get(in) : WEOF;
		if (c == WEOF || lower(c) != word[i]) {
			if (*w)
				unget(in, c);
			return i ? -1 : 0;
		}
		cb_put(b, c);
		(*w)--;
	}
	return 1;
}

static int scan_float(struct in *in, struct cbuf *b, size_t w)
{
	wint_t c = get(in);
	if ((c == '+' || c == '-') && w) {
		cb_put(b, c);
		w--;
		c = get(in);
	}
	if (w && lower(c) == 'i') {
		cb_put(b, c);
		w--;
		if (match_word(in, b, &w, "nf") != 1)
			return -1;
		c = w ? get(in) : WEOF;
		if (lower(c) != 'i') {
			if (w)
				unget(in, c);
			return 0;
		}
		cb_put(b, c);
		w--;
		return match_word(in, b, &w, "nity") == 1 ? 0 : -1;
	}
	if (w && lower(c) == 'n') {
		cb_put(b, c);
		w--;
		if (match_word(in, b, &w, "an") != 1)
			return -1;
		c = w ? get(in) : WEOF;
		if (c != '(') {
			if (w)
				unget(in, c);
			return 0;
		}
		cb_put(b, c);
		w--;
		for (;;) {
			c = w ? get(in) : WEOF;
			if (c == ')') {
				cb_put(b, c);
				return 0;
			}
			if (c == WEOF || !(dig(c) || (lower(c) >= 'a' && lower(c) <= 'z') || c == '_')) {
				unget(in, c);
				return 0;
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
		if (lower(c) == 'x' && w) {
			cb_put(b, c);
			w--;
			c = get(in);
			hex = 1;
			digits = 0;
		}
	}
	int dot = 0;
	for (; w; w--) {
		if (c == '.' && !dot)
			dot = 1;
		else if (hex ? hexdig(c) : dig(c))
			digits = 1;
		else
			break;
		cb_put(b, c);
		c = get(in);
	}
	if (!digits) {
		unget(in, c);
		return hex && !w ? 0 : -1;
	}
	if (w && lower(c) == (hex ? 'p' : 'e')) {
		cb_put(b, c);
		w--;
		c = w ? get(in) : WEOF;
		if ((c == '+' || c == '-') && w) {
			cb_put(b, c);
			w--;
			c = w ? get(in) : WEOF;
		}
		for (; w && dig(c); w--) {
			cb_put(b, c);
			c = get(in);
		}
		unget(in, c);
		return 0;
	}
	unget(in, c);
	return 0;
}

/* Storage for %c, %s and %[: wide, or multibyte, possibly allocated. */
struct sdest {
	char *s;
	wchar_t *ws;
	size_t n, cap;
	void **mptr;
	int wide, alloc, fail;
};

static void sd_room(struct sdest *d, size_t need)
{
	if (d->fail || !d->alloc || d->n + need < d->cap)
		return;
	size_t nc = d->cap ? d->cap : 32;
	while (nc <= d->n + need)
		nc *= 2;
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

static int sd_put(struct sdest *d, wchar_t c)
{
	if (d->wide) {
		sd_room(d, 1);
		if (!d->fail && d->ws)
			d->ws[d->n] = c;
		d->n++;
		return 0;
	}
	char b[MB_LEN_MAX];
	mbstate_t st = { 0 };
	size_t l = c ? wcrtomb(b, c, &st) : 1;
	if (l == (size_t)-1)
		return -1;
	if (!c)
		b[0] = 0;
	sd_room(d, l);
	if (!d->fail && d->s)
		memcpy(d->s + d->n, b, l);
	d->n += l;
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

/* Is c in the scanset [set, end)? The set is as written in the format:
 * ranges a-b, and ']' first is literal. */
static int in_set(const wchar_t *set, const wchar_t *end, wint_t c)
{
	for (const wchar_t *p = set; p < end; p++) {
		if (p + 2 < end && p[1] == '-') {
			if (c >= (wint_t)p[0] && c <= (wint_t)p[2])
				return 1;
			p += 2;
			continue;
		}
		if (c == (wint_t)*p)
			return 1;
	}
	return 0;
}

static int scan(struct in *in, const wchar_t *fmt, va_list ap)
{
	int matches = 0, positional = 0;
	va_list seq;
	va_copy(seq, ap);

	for (const wchar_t *p = fmt; *p; p++) {
		if (iswspace((wint_t)*p)) {
			while (iswspace((wint_t)p[1]))
				p++;
			wint_t c;
			while ((c = get(in)) != WEOF && iswspace(c)) ;
			unget(in, c);
			continue;
		}
		if (*p != '%' || p[1] == '%') {
			wint_t c;
			if (*p == '%') {
				p++;
				while ((c = get(in)) != WEOF && iswspace(c)) ;
			} else {
				c = get(in);
			}
			if (c != (wint_t)*p) {
				unget(in, c);
				if (c == WEOF)
					goto input_fail;
				goto done;
			}
			continue;
		}

		p++;
		void *dest = 0;
		int suppress = 0;
		if (*p == '*') {
			suppress = 1;
			p++;
		}
		if (!suppress && dig((wint_t)*p)) {
			const wchar_t *q = p;
			unsigned n = 0;
			while (dig((wint_t)*q) && n < 10000)
				n = n * 10 + (unsigned)(*q++ - '0');
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
		while (dig((wint_t)*p)) {
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
		wchar_t t = *p;
		if (!t)
			goto inval;
		if (!suppress && !dest && t != '%') {
			if (positional > 0)
				goto inval;
			positional = -1;
			dest = va_arg(seq, void *);
		}
		if (alloc && t != 's' && t != 'c' && t != '[' && t != 'S' && t != 'C')
			goto inval;
		if (suppress)
			dest = 0;

		if (t != '[' && t != 'c' && t != 'n' && t != 'C') {
			wint_t c;
			while ((c = get(in)) != WEOF && iswspace(c)) ;
			unget(in, c);
			if (c == WEOF)
				goto input_fail;
		}

		if (t == 'n') {
			store_int(dest, size, in->cnt);
			continue;
		}

		if (t == 'c' || t == 'C' || t == 's' || t == 'S' || t == '[') {
			int wide = size == S_L || t == 'C' || t == 'S';
			const wchar_t *set = 0, *set_end = 0;
			int invert = 0;
			if (t == '[') {
				p++;
				if (*p == '^') {
					invert = 1;
					p++;
				}
				set = p;
				if (*p == ']')
					p++;
				while (*p && *p != ']')
					p++;
				if (!*p)
					goto inval;
				set_end = p;
			}
			int is_c = t == 'c' || t == 'C';
			if (!have_width)
				width = is_c ? 1 : SIZE_MAX;

			struct sdest d = { 0 };
			d.wide = wide;
			d.alloc = alloc && dest;
			if (d.alloc)
				d.mptr = dest;
			else if (wide)
				d.ws = dest;
			else
				d.s = dest;
			size_t got = 0;
			wint_t c = 0;
			while (got < width) {
				c = get(in);
				if (c == WEOF)
					break;
				int ok = is_c || (t == '[' ? in_set(set, set_end, c) != invert : !iswspace(c));
				if (!ok) {
					unget(in, c);
					break;
				}
				if (sd_put(&d, (wchar_t)c) < 0) {
					errno = EILSEQ;
					goto fail_alloc;
				}
				got++;
			}
			if (!got) {
				if (c == WEOF)
					goto input_fail_alloc;
				goto fail_alloc;
			}
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
			base = scan_int(in, &b, width, base);
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
			if (scan_float(in, &b, width) < 0 || b.oom) {
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
	va_end(seq);
	return matches;
input_fail:
	va_end(seq);
	return matches ? matches : EOF;
inval:
	errno = EINVAL;
	va_end(seq);
	return matches ? matches : EOF;
}

int vfwscanf(FILE *__restrict f, const wchar_t *__restrict fmt, va_list ap)
{
	struct in in = { .f = f };
	FLOCK(f);
	if (!f->mode)
		f->mode = 1;
	int r = scan(&in, fmt, ap);
	if (in.has_pend)
		__ungetwc_unlocked(in.pend, f);
	FUNLOCK(f);
	return r;
}

int fwscanf(FILE *__restrict f, const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfwscanf(f, fmt, ap);
	va_end(ap);
	return r;
}

int vwscanf(const wchar_t *__restrict fmt, va_list ap)
{
	return vfwscanf(stdin, fmt, ap);
}

int wscanf(const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfwscanf(stdin, fmt, ap);
	va_end(ap);
	return r;
}

int vswscanf(const wchar_t *__restrict s, const wchar_t *__restrict fmt, va_list ap)
{
	struct in in = { .s = s };
	return scan(&in, fmt, ap);
}

int swscanf(const wchar_t *__restrict s, const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vswscanf(s, fmt, ap);
	va_end(ap);
	return r;
}
