/* Formatted wide output: the wprintf family.
 *
 * The format is parsed here, with the same conversions, flags and POSIX
 * positional arguments as printf. Characters and strings (%c %lc %s %ls
 * %C %S %m) are handled here, with field widths and precisions counted
 * in wide characters. Numbers and pointers are handed to the byte printf
 * engine through a small stream that widens its output one byte to one
 * wide character: that output is ASCII, and floating point keeps the
 * engine's exact rounding. As in printf, %n terminates the process. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define NL_ARGMAX 9

#define FL_LEFT  0x01
#define FL_PLUS  0x02
#define FL_SPACE 0x04
#define FL_ALT   0x08
#define FL_ZERO  0x10

enum {
	T_NONE, T_INT, T_UINT, T_LONG, T_ULONG, T_LLONG, T_ULLONG,
	T_SHORT, T_USHORT, T_CHAR, T_UCHAR, T_SIZE, T_IMAX, T_UMAX,
	T_PDIFF, T_PTR, T_DBL, T_LDBL,
};

union arg {
	uintmax_t i;
	long double f;
	void *p;
};

static void pop_arg(union arg *a, int type, va_list *ap)
{
	switch (type) {
	case T_INT: a->i = (uintmax_t)(intmax_t)va_arg(*ap, int); break;
	case T_UINT: a->i = va_arg(*ap, unsigned); break;
	case T_LONG: a->i = (uintmax_t)(intmax_t)va_arg(*ap, long); break;
	case T_ULONG: a->i = va_arg(*ap, unsigned long); break;
	case T_LLONG: a->i = (uintmax_t)(intmax_t)va_arg(*ap, long long); break;
	case T_ULLONG: a->i = va_arg(*ap, unsigned long long); break;
	case T_SHORT: a->i = (uintmax_t)(intmax_t)(short)va_arg(*ap, int); break;
	case T_USHORT: a->i = (unsigned short)va_arg(*ap, int); break;
	case T_CHAR: a->i = (uintmax_t)(intmax_t)(signed char)va_arg(*ap, int); break;
	case T_UCHAR: a->i = (unsigned char)va_arg(*ap, int); break;
	case T_SIZE: a->i = va_arg(*ap, size_t); break;
	case T_IMAX: a->i = (uintmax_t)va_arg(*ap, intmax_t); break;
	case T_UMAX: a->i = va_arg(*ap, uintmax_t); break;
	case T_PDIFF: a->i = (uintmax_t)(intmax_t)va_arg(*ap, ptrdiff_t); break;
	case T_PTR: a->p = va_arg(*ap, void *); break;
	case T_DBL: a->f = va_arg(*ap, double); break;
	case T_LDBL: a->f = va_arg(*ap, long double); break;
	}
}

/* ---- output: a stream, or a wide string buffer ---- */

struct wout {
	FILE *f;
	wchar_t *s;     /* swprintf: destination */
	size_t cap;     /* its size in wide characters */
	size_t pos;     /* wide characters produced */
	int err;
};

static void put1(struct wout *o, wchar_t c)
{
	if (o->err)
		return;
	if (o->f) {
		if (__fputwc_unlocked(c, o->f) == WEOF)
			o->err = 1;
	} else if (o->pos + 1 < o->cap) {
		o->s[o->pos] = c;
	}
	o->pos++;
}

static void putn(struct wout *o, const wchar_t *s, size_t n)
{
	for (size_t i = 0; i < n && !o->err; i++)
		put1(o, s[i]);
}

static void pad(struct wout *o, wchar_t c, size_t n)
{
	while (n-- && !o->err)
		put1(o, c);
}

/* The byte engine writes into this stream; its bytes are ASCII. */
static size_t widen_write(FILE *a, const unsigned char *s, size_t l)
{
	struct wout *o = a->cookie;
	for (const unsigned char *p = a->wbase; p < a->wpos; p++)
		put1(o, *p);
	for (size_t i = 0; i < l; i++)
		put1(o, s[i]);
	a->wpos = a->wbase = a->buf;
	a->wend = a->buf + a->buf_size;
	return l;
}

static int narrow_conv(struct wout *o, const char *spec, ...)
{
	unsigned char buf[UNGET + 256];
	FILE a;
	__file_init(&a, -1, F_NORD, buf, sizeof buf);
	a.write_fn = __f_fn(widen_write);
	a.cookie = o;
	va_list ap;
	va_start(ap, spec);
	int r = __vfprintf_unlocked(&a, spec, ap);
	va_end(ap);
	widen_write(&a, 0, 0);
	return r;
}

static int getint(const wchar_t **s)
{
	int i = 0;
	for (; (unsigned)**s - '0' < 10; (*s)++) {
		if (i > (INT_MAX - 9) / 10)
			i = -1; /* overflow: remembered, digits still consumed */
		else if (i >= 0)
			i = 10 * i + (int)(**s - '0');
	}
	return i;
}

/* Width of a narrow multibyte string in wide characters, at most max. */
static int mbs_count(const char *s, size_t max, size_t *out)
{
	mbstate_t st = { 0 };
	size_t n = 0;
	while (n < max) {
		wchar_t wc;
		size_t r = mbrtowc(&wc, s, 4, &st);
		if (r == (size_t)-1 || r == (size_t)-2)
			return -1;
		if (!r)
			break;
		s += r;
		n++;
	}
	*out = n;
	return 0;
}

static int wprintf_core(struct wout *o, const wchar_t *fmt, va_list *ap, union arg *nl_arg, int *nl_type,
                        int saved_errno)
{
	const wchar_t *s = fmt;
	int l10n = 0;
	size_t cnt = 0;
	int scan = !o;

#define ADD(n) do { size_t n_ = (size_t)(n); if (n_ > (size_t)INT_MAX - cnt) goto overflow; cnt += n_; } while (0)

	for (;;) {
		const wchar_t *a = s;
		while (*s && *s != '%')
			s++;
		size_t lit = (size_t)(s - a);
		if (s[0] == '%' && s[1] == '%') {
			ADD(lit + 1);
			if (!scan)
				putn(o, a, lit + 1);
			s += 2;
			continue;
		}
		ADD(lit);
		if (!scan)
			putn(o, a, lit);
		if (!*s)
			break;
		s++;

		int argpos = -1;
		if ((unsigned)*s - '0' < 10) {
			const wchar_t *t = s;
			int n = getint(&t);
			if (*t == '$') {
				if (n <= 0 || n > NL_ARGMAX)
					goto inval;
				argpos = n;
				s = t + 1;
			}
		}

		int fl = 0;
		for (;; s++) {
			if (*s == '-') fl |= FL_LEFT;
			else if (*s == '+') fl |= FL_PLUS;
			else if (*s == ' ') fl |= FL_SPACE;
			else if (*s == '#') fl |= FL_ALT;
			else if (*s == '0') fl |= FL_ZERO;
			else if (*s == '\'') ;
			else break;
		}

		int w = 0;
		if (*s == '*') {
			s++;
			if ((unsigned)*s - '0' < 10) {
				int n = getint(&s);
				if (*s != '$' || n <= 0 || n > NL_ARGMAX || l10n < 0)
					goto inval;
				l10n = 1;
				s++;
				if (scan)
					nl_type[n] = T_INT;
				else
					w = (int)nl_arg[n].i;
			} else {
				if (l10n > 0)
					goto inval;
				l10n = -1;
				if (scan)
					return 0;
				w = va_arg(*ap, int);
			}
			if (w < 0) {
				if (w == INT_MIN)
					goto overflow;
				fl |= FL_LEFT;
				w = -w;
			}
		} else {
			w = getint(&s);
			if (w < 0)
				goto overflow;
		}

		int p = -1;
		if (*s == '.') {
			s++;
			if (*s == '*') {
				s++;
				if ((unsigned)*s - '0' < 10) {
					int n = getint(&s);
					if (*s != '$' || n <= 0 || n > NL_ARGMAX || l10n < 0)
						goto inval;
					l10n = 1;
					s++;
					if (scan)
						nl_type[n] = T_INT;
					else
						p = (int)nl_arg[n].i;
				} else {
					if (l10n > 0)
						goto inval;
					l10n = -1;
					if (scan)
						return 0;
					p = va_arg(*ap, int);
				}
				if (p < 0)
					p = -1;
			} else {
				p = getint(&s);
				if (p < 0)
					goto overflow;
			}
		}

		enum { L_NONE, L_HH, L_H, L_L, L_LL, L_J, L_Z, L_T, L_BIGL } lm = L_NONE;
		switch (*s) {
		case 'h': s++; if (*s == 'h') { s++; lm = L_HH; } else lm = L_H; break;
		case 'l': s++; if (*s == 'l') { s++; lm = L_LL; } else lm = L_L; break;
		case 'q': s++; lm = L_LL; break;
		case 'j': s++; lm = L_J; break;
		case 'z': s++; lm = L_Z; break;
		case 't': s++; lm = L_T; break;
		case 'L': s++; lm = L_BIGL; break;
		}

		wchar_t c = *s++;
		int type = T_NONE;
		switch (c) {
		case 'd': case 'i':
			type = lm == L_HH ? T_CHAR : lm == L_H ? T_SHORT : lm == L_L ? T_LONG :
			       lm == L_LL ? T_LLONG : lm == L_J ? T_IMAX : lm == L_Z ? T_SIZE :
			       lm == L_T ? T_PDIFF : lm == L_NONE ? T_INT : T_NONE;
			break;
		case 'o': case 'u': case 'x': case 'X':
			type = lm == L_HH ? T_UCHAR : lm == L_H ? T_USHORT : lm == L_L ? T_ULONG :
			       lm == L_LL ? T_ULLONG : lm == L_J ? T_UMAX : lm == L_Z ? T_SIZE :
			       lm == L_T ? T_PDIFF : lm == L_NONE ? T_UINT : T_NONE;
			break;
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
			type = lm == L_BIGL ? T_LDBL : (lm == L_NONE || lm == L_L) ? T_DBL : T_NONE;
			break;
		case 'c':
			type = lm == L_NONE ? T_INT : lm == L_L ? T_UINT : T_NONE;
			break;
		case 'C':
			type = lm == L_NONE ? T_UINT : T_NONE;
			break;
		case 's': case 'S': case 'p':
			type = (lm == L_NONE || (lm == L_L && c == 's')) ? T_PTR : T_NONE;
			break;
		case 'm':
		case '%':
			if (lm != L_NONE)
				goto inval;
			break;
		case 'n':
			__fatal("%n in wprintf format string");
		default:
			goto inval;
		}
		if (type == T_NONE && c != 'm' && c != '%')
			goto inval;

		union arg arg = { 0 };
		if (c != 'm' && c != '%') {
			if (argpos > 0) {
				if (l10n < 0)
					goto inval;
				l10n = 1;
				if (scan) {
					if (nl_type[argpos] && nl_type[argpos] != type)
						goto inval;
					nl_type[argpos] = type;
				} else {
					arg = nl_arg[argpos];
				}
			} else {
				if (l10n > 0)
					goto inval;
				l10n = -1;
				if (scan)
					return 0;
				pop_arg(&arg, type, ap);
			}
		} else if (argpos > 0) {
			goto inval;
		}
		if (scan)
			continue;

		switch (c) {
		case '%':
			ADD(1);
			put1(o, L'%');
			break;
		case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'p':
		case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
			char spec[16], *q = spec;
			*q++ = '%';
			if (fl & FL_LEFT) *q++ = '-';
			if (fl & FL_PLUS) *q++ = '+';
			if (fl & FL_SPACE) *q++ = ' ';
			if (fl & FL_ALT) *q++ = '#';
			if (fl & FL_ZERO) *q++ = '0';
			*q++ = '*';
			*q++ = '.';
			*q++ = '*';
			int r;
			if (c == 'p') {
				*q++ = 'p';
				*q = 0;
				r = narrow_conv(o, spec, w, p, arg.p);
			} else if (c == 'd' || c == 'i') {
				*q++ = 'j';
				*q++ = (char)c;
				*q = 0;
				r = narrow_conv(o, spec, w, p, (intmax_t)arg.i);
			} else if (c == 'o' || c == 'u' || c == 'x' || c == 'X') {
				*q++ = 'j';
				*q++ = (char)c;
				*q = 0;
				r = narrow_conv(o, spec, w, p, arg.i);
			} else {
				*q++ = 'L';
				*q++ = (char)c;
				*q = 0;
				r = narrow_conv(o, spec, w, p, arg.f);
			}
			if (r < 0)
				return -1; /* errno set by the byte engine */
			ADD(r);
			break;
		}
		case 'c':
			if (lm == L_NONE) {
				wint_t wc = btowc((int)arg.i);
				if (wc == WEOF)
					goto ilseq;
				arg.i = wc;
			}
			__attribute__((__fallthrough__));
		case 'C': {
			size_t fill = (size_t)w > 1 ? (size_t)w - 1 : 0;
			ADD(fill + 1);
			if (!(fl & FL_LEFT))
				pad(o, L' ', fill);
			put1(o, (wchar_t)arg.i);
			if (fl & FL_LEFT)
				pad(o, L' ', fill);
			break;
		}
		case 'S':
			goto wide;
		case 'm':
			arg.p = strerror(saved_errno);
			__attribute__((__fallthrough__));
		case 's':
			if (c == 's' && lm == L_L)
				goto wide;
			{
				/* a multibyte string: count, then convert as written */
				const char *ms = arg.p ? arg.p : (p >= 0 && p < 6) ? "" : "(null)";
				size_t n;
				if (mbs_count(ms, p >= 0 ? (size_t)p : SIZE_MAX, &n) < 0)
					goto ilseq;
				size_t fill = (size_t)w > n ? (size_t)w - n : 0;
				ADD(n + fill);
				if (!(fl & FL_LEFT))
					pad(o, L' ', fill);
				mbstate_t st = { 0 };
				for (size_t i = 0; i < n; i++) {
					wchar_t wc;
					ms += mbrtowc(&wc, ms, 4, &st);
					put1(o, wc);
				}
				if (fl & FL_LEFT)
					pad(o, L' ', fill);
			}
			break;
		wide: {
			const wchar_t *ws = arg.p ? arg.p : (p >= 0 && p < 6) ? L"" : L"(null)";
			size_t n = p >= 0 ? wcsnlen(ws, (size_t)p) : wcslen(ws);
			size_t fill = (size_t)w > n ? (size_t)w - n : 0;
			ADD(n + fill);
			if (!(fl & FL_LEFT))
				pad(o, L' ', fill);
			putn(o, ws, n);
			if (fl & FL_LEFT)
				pad(o, L' ', fill);
			break;
		}
		}
	}

	if (scan) {
		if (l10n <= 0)
			return 0;
		int i;
		for (i = 1; i <= NL_ARGMAX && nl_type[i]; i++)
			pop_arg(&nl_arg[i], nl_type[i], ap);
		for (; i <= NL_ARGMAX; i++)
			if (nl_type[i])
				goto inval;
		return 0;
	}
	if (o->err)
		return -1;
	return (int)cnt;

inval:
	errno = EINVAL;
	return -1;
overflow:
	errno = EOVERFLOW;
	return -1;
ilseq:
	errno = EILSEQ;
	return -1;
#undef ADD
}

static int run(struct wout *o, const wchar_t *fmt, va_list ap)
{
	int saved_errno = errno;
	int nl_type[NL_ARGMAX + 1] = { 0 };
	union arg nl_arg[NL_ARGMAX + 1];
	va_list ap2;
	va_copy(ap2, ap);
	int r = wprintf_core(0, fmt, &ap2, nl_arg, nl_type, saved_errno);
	va_end(ap2);
	if (r < 0)
		return -1;
	va_copy(ap2, ap);
	r = wprintf_core(o, fmt, &ap2, nl_arg, nl_type, saved_errno);
	va_end(ap2);
	return r;
}

int vfwprintf(FILE *__restrict f, const wchar_t *__restrict fmt, va_list ap)
{
	FLOCK(f);
	if (!f->mode)
		f->mode = 1;
	unsigned olderr = f->flags & F_ERR;
	f->flags &= ~F_ERR;
	/* an unbuffered stream gets one write per call, not one per piece */
	unsigned char tmp[256];
	unsigned char *saved = 0;
	if (!f->buf_size) {
		saved = f->buf;
		f->buf = tmp;
		f->buf_size = sizeof tmp;
		f->wpos = f->wbase = f->wend = 0;
	}
	int r;
	if (!f->wend && __towrite(f)) {
		r = -1;
	} else {
		struct wout o = { .f = f };
		r = run(&o, fmt, ap);
	}
	if (saved) {
		if (f->wpos != f->wbase)
			f_write(f, 0, 0);
		f->buf = saved;
		f->buf_size = 0;
		f->wpos = f->wbase = f->wend = 0;
	}
	if (f->flags & F_ERR)
		r = -1;
	f->flags |= olderr;
	FUNLOCK(f);
	return r;
}

int fwprintf(FILE *__restrict f, const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfwprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

int vwprintf(const wchar_t *__restrict fmt, va_list ap)
{
	return vfwprintf(stdout, fmt, ap);
}

int wprintf(const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vfwprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}

/* swprintf: at most n wide characters including the terminator. Unlike
 * snprintf, output that does not fit is an error (the text written is
 * still terminated). */
int vswprintf(wchar_t *__restrict s, size_t n, const wchar_t *__restrict fmt, va_list ap)
{
	if (!n) {
		errno = EOVERFLOW;
		return -1;
	}
	struct wout o = { .s = s, .cap = n };
	int r = run(&o, fmt, ap);
	s[o.pos < n ? o.pos : n - 1] = 0;
	if (r >= 0 && (size_t)r >= n) {
		errno = EOVERFLOW;
		return -1;
	}
	return r;
}

int swprintf(wchar_t *__restrict s, size_t n, const wchar_t *__restrict fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vswprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
