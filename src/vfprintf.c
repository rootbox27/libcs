/* Formatted output.
 *
 * Supports the C11 conversions, POSIX positional arguments (%n$, *n$),
 * the XSI %C/%S aliases and the GNU %m extension. Floating-point values
 * are converted exactly (big-integer decimal expansion) and rounded to
 * nearest, ties to even, for both double and long double.
 *
 * Hardening: %n is not supported. A format containing it terminates the
 * process, since its only real-world use is exploiting format-string
 * bugs. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#define NL_ARGMAX 9

/* flags */
#define FL_LEFT  0x01
#define FL_PLUS  0x02
#define FL_SPACE 0x04
#define FL_ALT   0x08
#define FL_ZERO  0x10
#define FL_UPPER 0x20

/* argument types */
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

/* ---- output helpers ---- */

struct out {
	FILE *f;
	int err;
};

static void out(struct out *o, const char *s, size_t l)
{
	if (!o->err && !(o->f->flags & F_ERR) && __fwritex((const unsigned char *)s, l, o->f) != l)
		o->err = 1;
}

static void pad(struct out *o, char c, size_t n)
{
	char b[64];
	memset(b, c, n < sizeof b ? n : sizeof b);
	while (n) {
		size_t k = n < sizeof b ? n : sizeof b;
		out(o, b, k);
		n -= k;
	}
}

/* Emit prefix, zero padding and body within a field of width w:
 *   [spaces][prefix][zeros to width if FL_ZERO][zeros to prec][body][spaces]
 * `zeros` is extra leading zeros required by the precision. */
static void emit_field(struct out *o, int fl, size_t w, const char *pre, size_t pl,
                       size_t zeros, const char *body, size_t bl)
{
	size_t len = pl + zeros + bl;
	size_t fill = w > len ? w - len : 0;
	if (!(fl & FL_LEFT) && !(fl & FL_ZERO))
		pad(o, ' ', fill);
	out(o, pre, pl);
	if (!(fl & FL_LEFT) && (fl & FL_ZERO))
		pad(o, '0', fill);
	pad(o, '0', zeros);
	out(o, body, bl);
	if (fl & FL_LEFT)
		pad(o, ' ', fill);
}

/* ---- integers ---- */

static char *fmt_u(uintmax_t x, char *end, unsigned base, int upper)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	do *--end = digits[x % base]; while (x /= base);
	return end;
}

/* ---- floating point ---- */

/* Exact decimal representation of a non-negative finite value:
 * value = 0.D[0]D[1]... x 10^E, with D given by big number N in base 1e9
 * (least significant limb first). Rounding state: digits at index >= K
 * are dropped; if r >= 0 digit r is incremented and later digits become
 * 0; if carry is set the result is 10^E (a single leading 1). */
#define LIMB 1000000000u
#define MAX_LIMBS (16500 / 9 + 8) /* 5^16445 * 2^64 has ~11515 digits */

struct dec {
	uint32_t L[MAX_LIMBS];
	int n, nd;
	int E0, E;      /* exponent before / after rounding */
	int K, r, carry;
};

static const uint32_t p10[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

static void big_mul(struct dec *d, uint32_t m)
{
	uint64_t carry = 0;
	for (int i = 0; i < d->n; i++) {
		uint64_t t = (uint64_t)d->L[i] * m + carry;
		d->L[i] = (uint32_t)(t % LIMB);
		carry = t / LIMB;
	}
	while (carry) {
		d->L[d->n++] = (uint32_t)(carry % LIMB);
		carry /= LIMB;
	}
}

/* value = m * 2^e */
static void dec_init(struct dec *d, uint64_t m, int e)
{
	d->K = INT_MAX;
	d->r = -1;
	d->carry = 0;
	if (!m) {
		d->L[0] = 0;
		d->n = 1;
		d->nd = 1;
		d->E0 = d->E = 1;
		return;
	}
	int tz = __builtin_ctzll(m);
	m >>= tz;
	e += tz;
	d->n = 0;
	do {
		d->L[d->n++] = (uint32_t)(m % LIMB);
		m /= LIMB;
	} while (m);
	int dexp = 0;
	if (e >= 0) {
		for (; e >= 29; e -= 29)
			big_mul(d, 1u << 29);
		if (e)
			big_mul(d, 1u << e);
	} else {
		/* m / 2^k = m * 5^k / 10^k */
		int k = -e;
		dexp = -k;
		for (; k >= 13; k -= 13)
			big_mul(d, 1220703125u); /* 5^13 */
		uint32_t p = 1;
		while (k--)
			p *= 5;
		big_mul(d, p);
	}
	int top = 1;
	while (top < 10 && d->L[d->n - 1] >= p10[top])
		top++;
	d->nd = top + 9 * (d->n - 1);
	d->E0 = d->E = d->nd + dexp;
}

static int dig(const struct dec *d, int i)
{
	if (i < 0 || i >= d->nd)
		return 0;
	int j = d->nd - 1 - i;
	return (int)(d->L[j / 9] / p10[j % 9] % 10);
}

/* Any nonzero digit at index > i? */
static int sticky_after(const struct dec *d, int i)
{
	for (int k = i + 1; k < d->nd; k++)
		if (dig(d, k))
			return 1;
	return 0;
}

/* Keep K significant digits, rounding to nearest, ties to even. */
static void dec_round(struct dec *d, int K)
{
	d->K = K;
	d->r = -1;
	d->carry = 0;
	d->E = d->E0;
	if (K >= d->nd)
		return;
	if (K < 0)
		return; /* value < half a unit: rounds to zero */
	int h = dig(d, K);
	int up = h > 5 || (h == 5 && (sticky_after(d, K) || (K > 0 && (dig(d, K - 1) & 1))));
	if (!up)
		return;
	int i = K - 1;
	while (i >= 0 && dig(d, i) == 9)
		i--;
	if (i < 0) {
		d->carry = 1;
		d->E++;
	} else {
		d->r = i;
	}
}

/* Digit i of the rounded value. */
static int rdig(const struct dec *d, int i)
{
	if (d->carry)
		return i == 0;
	if (i < 0 || i >= d->K)
		return 0;
	if (d->r >= 0 && i >= d->r)
		return i == d->r ? dig(d, i) + 1 : 0;
	return dig(d, i);
}

/* Buffered digit emission. */
struct dbuf {
	struct out *o;
	char b[128];
	size_t n;
};
static void db_put(struct dbuf *b, char c)
{
	if (b->n == sizeof b->b) {
		out(b->o, b->b, b->n);
		b->n = 0;
	}
	b->b[b->n++] = c;
}
static void db_flush(struct dbuf *b)
{
	out(b->o, b->b, b->n);
	b->n = 0;
}

struct ldparts {
	uint64_t m;
	int e;          /* value = m * 2^e */
	int neg, inf, nan;
};

static struct ldparts ld_split(long double y)
{
	union {
		long double f;
		struct { uint64_t m; uint16_t se; } i;
	} u = { .f = y };
	struct ldparts p = { 0 };
	int ex = u.i.se & 0x7fff;
	p.neg = u.i.se >> 15;
	p.m = u.i.m;
	if (ex == 0x7fff) {
		if (p.m << 1)
			p.nan = 1;
		else
			p.inf = 1;
	} else if (ex == 0) {
		p.e = 1 - 16383 - 63;
	} else {
		if (!(p.m >> 63)) /* unnormal: invalid encoding */
			p.nan = 1;
		p.e = ex - 16383 - 63;
	}
	return p;
}

static size_t exp_str(char *end, int x, char letter, int min_digits, char **start)
{
	char *p = end;
	unsigned ux = x < 0 ? (unsigned)-x : (unsigned)x;
	int n = 0;
	do {
		*--p = (char)('0' + ux % 10);
		n++;
	} while (ux /= 10);
	while (n++ < min_digits)
		*--p = '0';
	*--p = x < 0 ? '-' : '+';
	*--p = letter;
	*start = p;
	return (size_t)(end - p);
}

static size_t fmt_hex(struct out *o, struct ldparts *v, size_t w, int p, int fl, const char *pre, size_t pl, size_t room)
{
	/* Normalise to 1.f x 2^X with 63 fraction bits in f. */
	uint64_t m = v->m;
	int X = 0;
	if (m) {
		int s = __builtin_clzll(m);
		m <<= s;
		X = v->e + 63 - s;
	}
	uint64_t frac = m << 1; /* 64 bits: 16 hex digits */
	int lead = m ? 1 : 0;
	if (p >= 0 && p < 16) {
		int drop = 64 - 4 * p;
		uint64_t keep = drop == 64 ? 0 : frac >> drop;
		uint64_t rem = drop == 64 ? frac : frac << (64 - drop);
		uint64_t half = 1ULL << 63;
		if (rem > half || (rem == half && ((p ? keep : (uint64_t)lead) & 1))) {
			keep++;
			if (p == 0 || keep >> (4 * p)) {
				keep = 0;
				lead++;
			}
		}
		if (lead == 2) {
			lead = 1;
			X++;
		}
		frac = drop == 64 ? 0 : keep << drop;
	}
	int nd = 16;
	if (p < 0) {
		while (nd && !((frac >> (64 - 4 * nd)) & 15))
			nd--;
		p = nd;
	}
	const char *xd = (fl & FL_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
	char body[16 + 2 + 16];
	size_t bl = 0;
	body[bl++] = xd[lead];
	int ndig = p < 16 ? p : 16;
	if (p || (fl & FL_ALT))
		body[bl++] = '.';
	for (int i = 0; i < ndig; i++)
		body[bl++] = xd[(frac >> (60 - 4 * i)) & 15];
	char eb[16], *es;
	size_t el = exp_str(eb + sizeof eb, m ? X : 0, (fl & FL_UPPER) ? 'P' : 'p', 1, &es);
	char pfx[4];
	memcpy(pfx, pre, pl);
	pfx[pl] = '0';
	pfx[pl + 1] = (fl & FL_UPPER) ? 'X' : 'x';
	size_t trailing = (size_t)(p - ndig);
	size_t len = pl + 2 + bl + trailing + el;
	size_t fill = w > len ? w - len : 0;
	if (len + fill > room)
		return len + fill;
	if (!(fl & (FL_LEFT | FL_ZERO)))
		pad(o, ' ', fill);
	out(o, pfx, pl + 2);
	if ((fl & FL_ZERO) && !(fl & FL_LEFT))
		pad(o, '0', fill);
	out(o, body, bl);
	pad(o, '0', trailing);
	out(o, es, el);
	if (fl & FL_LEFT)
		pad(o, ' ', fill);
	return len + fill;
}

/* Format a floating-point conversion; returns the field length. Nothing
 * is written if that length exceeds room. */
static size_t fmt_fp(struct out *o, long double y, size_t w, int p, int fl, int t, size_t room)
{
	struct ldparts v = ld_split(y);
	char pre[3];
	size_t pl = 0;
	if (v.neg)
		pre[pl++] = '-';
	else if (fl & FL_PLUS)
		pre[pl++] = '+';
	else if (fl & FL_SPACE)
		pre[pl++] = ' ';

	if (v.inf || v.nan) {
		const char *s = v.nan ? ((fl & FL_UPPER) ? "NAN" : "nan") : ((fl & FL_UPPER) ? "INF" : "inf");
		size_t n = w > pl + 3 ? w : pl + 3;
		if (n <= room)
			emit_field(o, fl & ~FL_ZERO, w, pre, pl, 0, s, 3);
		return n;
	}
	if (t == 'a')
		return fmt_hex(o, &v, w, p, fl, pre, pl, room);
	if (p < 0)
		p = 6;

	struct dec d;
	dec_init(&d, v.m, v.e);

	int style = t; /* 'e' or 'f' */
	int strip = 0;
	if (t == 'g') {
		int P = p ? p : 1;
		dec_round(&d, P);
		int X = v.m ? d.E - 1 : 0;
		if (P > X && X >= -4) {
			style = 'f';
			p = P - 1 - X;
		} else {
			style = 'e';
			p = P - 1;
		}
		strip = !(fl & FL_ALT);
	}

	if (style == 'e') {
		dec_round(&d, p + 1);
		int X = v.m ? d.E - 1 : 0;
		int nfrac = p;
		if (strip)
			while (nfrac > 0 && rdig(&d, nfrac) == 0)
				nfrac--;
		char eb[16], *es;
		size_t el = exp_str(eb + sizeof eb, X, (fl & FL_UPPER) ? 'E' : 'e', 2, &es);
		int dot = nfrac || (fl & FL_ALT);
		size_t len = pl + 1 + (size_t)dot + (size_t)nfrac + el;
		size_t fill = w > len ? w - len : 0;
		if (len + fill > room)
			return len + fill;
		if (!(fl & (FL_LEFT | FL_ZERO)))
			pad(o, ' ', fill);
		out(o, pre, pl);
		if ((fl & FL_ZERO) && !(fl & FL_LEFT))
			pad(o, '0', fill);
		struct dbuf b = { .o = o };
		db_put(&b, (char)('0' + rdig(&d, 0)));
		if (dot)
			db_put(&b, '.');
		for (int i = 1; i <= nfrac; i++)
			db_put(&b, (char)('0' + rdig(&d, i)));
		db_flush(&b);
		out(o, es, el);
		if (fl & FL_LEFT)
			pad(o, ' ', fill);
		return len + fill;
	}

	/* 'f': digits with index < E are the integer part. The number of
	 * digits kept is counted from the unrounded exponent. */
	if (!v.m || (long)d.E0 + p < 0)
		dec_round(&d, -1);
	else
		dec_round(&d, (int)((long)d.E0 + p > INT_MAX ? INT_MAX : (long)d.E0 + p));
	int E = d.E;
	int nint = E > 0 ? E : 1;
	int nfrac = p;
	if (strip)
		while (nfrac > 0 && rdig(&d, E + nfrac - 1) == 0)
			nfrac--;
	int dot = nfrac || (fl & FL_ALT);
	size_t len = pl + (size_t)nint + (size_t)dot + (size_t)nfrac;
	size_t fill = w > len ? w - len : 0;
	if (len + fill > room)
		return len + fill;
	if (!(fl & (FL_LEFT | FL_ZERO)))
		pad(o, ' ', fill);
	out(o, pre, pl);
	if ((fl & FL_ZERO) && !(fl & FL_LEFT))
		pad(o, '0', fill);
	struct dbuf b = { .o = o };
	if (E > 0)
		for (int i = 0; i < E; i++)
			db_put(&b, (char)('0' + rdig(&d, i)));
	else
		db_put(&b, '0');
	if (dot)
		db_put(&b, '.');
	for (int i = 0; i < nfrac; i++)
		db_put(&b, (char)('0' + rdig(&d, E + i)));
	db_flush(&b);
	if (fl & FL_LEFT)
		pad(o, ' ', fill);
	return len + fill;
}

/* ---- wide characters ---- */

static int utf8(char *s, unsigned c)
{
	if (c < 0x80) {
		s[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		s[0] = (char)(0xc0 | c >> 6);
		s[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (c < 0x10000) {
		if (c - 0xd800 < 0x800)
			return -1;
		s[0] = (char)(0xe0 | c >> 12);
		s[1] = (char)(0x80 | (c >> 6 & 0x3f));
		s[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if (c < 0x110000) {
		s[0] = (char)(0xf0 | c >> 18);
		s[1] = (char)(0x80 | (c >> 12 & 0x3f));
		s[2] = (char)(0x80 | (c >> 6 & 0x3f));
		s[3] = (char)(0x80 | (c & 0x3f));
		return 4;
	}
	return -1;
}

/* ---- the format interpreter ---- */

static int getint(const char **s)
{
	int n = 0;
	for (; (unsigned)**s - '0' < 10; (*s)++) {
		int d = **s - '0';
		if (n > (INT_MAX - d) / 10)
			n = -1; /* keep consuming, report overflow */
		else if (n >= 0)
			n = 10 * n + d;
	}
	return n;
}

/* One pass over the format. With f == NULL this is the scanning pass:
 * it records positional argument types and, if the format uses them,
 * fetches all arguments in order. Returns the character count, or -1
 * with errno set (EINVAL, EOVERFLOW, EILSEQ). */
static int printf_core(FILE *f, const char *fmt, va_list *ap, union arg *nl_arg, int *nl_type, int saved_errno)
{
	struct out o = { f, 0 };
	const unsigned char *s = (const unsigned char *)fmt;
	int cnt = 0;
	int l10n = 0; /* 1: positional args used, -1: sequential */
	char ibuf[3 * sizeof(uintmax_t) + 3];

#define ROOM ((size_t)(INT_MAX - cnt))
/* Check a field fits before emitting it, so an oversized request fails
 * without writing gigabytes first. */
#define FITS(n) do { if ((size_t)(n) > ROOM) goto overflow; } while (0)
#define ADD(n) do { size_t n_ = (size_t)(n); if (n_ > (size_t)(INT_MAX - cnt)) goto overflow; cnt += (int)n_; } while (0)

	for (;;) {
		const unsigned char *a = s;
		while (*s && *s != '%')
			s++;
		if (s[0] == '%' && s[1] == '%') {
			FITS(s - a + 1);
			if (f)
				out(&o, (const char *)a, (size_t)(s - a) + 1);
			ADD(s - a + 1);
			s += 2;
			continue;
		}
		FITS(s - a);
		if (f)
			out(&o, (const char *)a, (size_t)(s - a));
		ADD(s - a);
		if (!*s)
			break;
		s++;

		/* argument position */
		int argpos = -1;
		if ((unsigned)*s - '0' < 10) {
			const char *t = (const char *)s;
			int n = getint(&t);
			if (*t == '$') {
				if (n <= 0 || n > NL_ARGMAX)
					goto inval;
				argpos = n;
				s = (const unsigned char *)t + 1;
			}
		}

		/* flags */
		int fl = 0;
		for (;; s++) {
			if (*s == '-') fl |= FL_LEFT;
			else if (*s == '+') fl |= FL_PLUS;
			else if (*s == ' ') fl |= FL_SPACE;
			else if (*s == '#') fl |= FL_ALT;
			else if (*s == '0') fl |= FL_ZERO;
			else if (*s == '\'') ; /* grouping: none in the C locale */
			else break;
		}

		/* width */
		int w = 0;
		if (*s == '*') {
			s++;
			if ((unsigned)*s - '0' < 10) {
				const char *t = (const char *)s;
				int n = getint(&t);
				if (*t != '$' || n <= 0 || n > NL_ARGMAX || l10n < 0)
					goto inval;
				l10n = 1;
				s = (const unsigned char *)t + 1;
				if (!f) {
					nl_type[n] = T_INT;
					w = 0;
				} else {
					w = (int)nl_arg[n].i;
				}
			} else {
				if (l10n > 0)
					goto inval;
				l10n = -1;
				if (!f)
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
			w = getint((const char **)&s);
			if (w < 0)
				goto overflow;
		}

		/* precision */
		int p = -1;
		if (*s == '.') {
			s++;
			if (*s == '*') {
				s++;
				if ((unsigned)*s - '0' < 10) {
					const char *t = (const char *)s;
					int n = getint(&t);
					if (*t != '$' || n <= 0 || n > NL_ARGMAX || l10n < 0)
						goto inval;
					l10n = 1;
					s = (const unsigned char *)t + 1;
					if (!f)
						nl_type[n] = T_INT;
					else
						p = (int)nl_arg[n].i;
				} else {
					if (l10n > 0)
						goto inval;
					l10n = -1;
					if (!f)
						return 0;
					p = va_arg(*ap, int);
				}
				if (p < 0)
					p = -1;
			} else {
				p = getint((const char **)&s);
				if (p < 0)
					goto overflow;
			}
		}

		/* length modifier */
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

		int c = *s++;
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
		case '%': /* "%5%": glibc prints '%' and ignores the field */
			if (lm != L_NONE)
				goto inval;
			break;
		case 'n':
			__fatal("%n in printf format string");
		default:
			goto inval;
		}
		if (type == T_NONE && c != 'm' && c != '%')
			goto inval;

		/* fetch the argument */
		union arg arg = { 0 };
		if (c != 'm' && c != '%') {
			if (argpos > 0) {
				if (l10n < 0)
					goto inval;
				l10n = 1;
				if (!f) {
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
				if (!f)
					return 0;
				pop_arg(&arg, type, ap);
			}
		} else if (argpos > 0) {
			goto inval;
		}
		if (!f)
			continue;

		const char *pre = "";
		size_t pl = 0;
		char *z = ibuf + sizeof ibuf, *d;
		size_t len;
		switch (c) {
		case '%':
			out(&o, "%", 1);
			ADD(1);
			break;
		case 'd': case 'i': {
			intmax_t v = (intmax_t)arg.i;
			uintmax_t u = v < 0 ? -(uintmax_t)v : (uintmax_t)v;
			if (v < 0) { pre = "-"; pl = 1; }
			else if (fl & FL_PLUS) { pre = "+"; pl = 1; }
			else if (fl & FL_SPACE) { pre = " "; pl = 1; }
			d = (p == 0 && u == 0) ? z : fmt_u(u, z, 10, 0);
			goto integer;
		}
		case 'u':
			d = (p == 0 && arg.i == 0) ? z : fmt_u(arg.i, z, 10, 0);
			goto integer;
		case 'o':
			d = (p == 0 && arg.i == 0) ? z : fmt_u(arg.i, z, 8, 0);
			/* '#': increase precision so the first digit is 0 (after
			 * deciding on zero padding, which only an explicit
			 * precision disables) */
			if (p >= 0)
				fl &= ~FL_ZERO;
			if ((fl & FL_ALT) && (d == z || *d != '0') && p < z - d + 1)
				p = (int)(z - d) + 1;
			goto integer;
		case 'x': case 'X':
			d = (p == 0 && arg.i == 0) ? z : fmt_u(arg.i, z, 16, c == 'X');
			if ((fl & FL_ALT) && arg.i) {
				pre = c == 'X' ? "0X" : "0x";
				pl = 2;
			}
		integer:
			len = (size_t)(z - d);
			if (p >= 0 && c != 'o')
				fl &= ~FL_ZERO;
			{
				size_t zeros = p > 0 && (size_t)p > len ? (size_t)p - len : 0;
				size_t total = pl + zeros + len;
				FITS(total > (size_t)w ? total : (size_t)w);
				emit_field(&o, fl, (size_t)w, pre, pl, zeros, d, len);
				ADD(total > (size_t)w ? total : (size_t)w);
			}
			break;
		case 'p':
			if (!arg.p) {
				FITS(w > 5 ? w : 5);
				emit_field(&o, fl & ~FL_ZERO, (size_t)w, "", 0, 0, "(nil)", 5);
				ADD(w > 5 ? w : 5);
				break;
			}
			d = fmt_u((uintptr_t)arg.p, z, 16, 0);
			pre = "0x";
			pl = 2;
			goto integer;
		case 'c':
			if (lm == L_NONE) {
				char ch = (char)arg.i;
				FITS(w > 1 ? w : 1);
				emit_field(&o, fl & ~FL_ZERO, (size_t)w, "", 0, 0, &ch, 1);
				ADD(w > 1 ? w : 1);
				break;
			}
			/* %lc */
			__attribute__((__fallthrough__));
		case 'C': {
			char mb[4];
			if (arg.i == 0) {
				mb[0] = 0;
				len = 1;
			} else {
				int k = utf8(mb, (unsigned)arg.i);
				if (k < 0)
					goto ilseq;
				len = (size_t)k;
			}
			FITS((size_t)w > len ? (size_t)w : len);
			emit_field(&o, fl & ~FL_ZERO, (size_t)w, "", 0, 0, mb, len);
			ADD((size_t)w > len ? (size_t)w : len);
			break;
		}
		case 'm':
			arg.p = strerror(saved_errno);
			__attribute__((__fallthrough__));
		case 's':
			if (c == 's' && lm == L_L)
				goto wide;
			/* NULL prints "(null)", or nothing if the precision
			 * would truncate it (as glibc does). */
			d = arg.p ? arg.p : (p >= 0 && p < 6) ? "" : "(null)";
			len = p >= 0 ? strnlen(d, (size_t)p) : strlen(d);
			FITS((size_t)w > len ? (size_t)w : len);
			emit_field(&o, fl & ~FL_ZERO, (size_t)w, "", 0, 0, d, len);
			ADD((size_t)w > len ? (size_t)w : len);
			break;
		case 'S':
		wide: {
			const wchar_t *ws = arg.p ? arg.p : L"(null)";
			/* First measure: whole characters that fit in precision. */
			size_t bytes = 0;
			char mb[4];
			for (const wchar_t *q = ws; *q; q++) {
				int k = utf8(mb, (unsigned)*q);
				if (k < 0)
					goto ilseq;
				if (p >= 0 && bytes + (size_t)k > (size_t)p)
					break;
				bytes += (size_t)k;
				if (bytes > (size_t)INT_MAX)
					goto overflow;
			}
			size_t fill = (size_t)w > bytes ? (size_t)w - bytes : 0;
			FITS(bytes + fill);
			if (!(fl & FL_LEFT))
				pad(&o, ' ', fill);
			for (size_t done = 0; done < bytes; ws++) {
				int k = utf8(mb, (unsigned)*ws);
				out(&o, mb, (size_t)k);
				done += (size_t)k;
			}
			if (fl & FL_LEFT)
				pad(&o, ' ', fill);
			ADD(bytes + fill);
			break;
		}
		case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
			if (c >= 'A' && c <= 'Z')
				fl |= FL_UPPER;
			ADD(fmt_fp(&o, arg.f, (size_t)w, p, fl, c | 32, ROOM));
			break;
		}
		}
	}

	if (!f) {
		/* Scanning pass: fetch positional arguments in order. */
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
	if (o.err)
		return -1;
	return cnt;

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
#undef FITS
#undef ROOM
}

hidden int __vfprintf_unlocked(FILE *f, const char *fmt, va_list ap)
{
	int saved_errno = errno;
	int nl_type[NL_ARGMAX + 1] = { 0 };
	union arg nl_arg[NL_ARGMAX + 1];
	va_list ap2;

	/* Scanning pass: validates positional usage and collects those
	 * arguments. Formats without them stop at the first conversion. */
	va_copy(ap2, ap);
	int ret = printf_core(0, fmt, &ap2, nl_arg, nl_type, saved_errno);
	va_end(ap2);
	if (ret < 0)
		return -1;

	unsigned olderr = f->flags & F_ERR;
	f->flags &= ~F_ERR;
	/* Unbuffered stream: format into a temporary buffer so a single
	 * printf becomes one write, not one per piece. */
	unsigned char tmp[256];
	unsigned char *saved = 0;
	if (!f->buf_size) {
		saved = f->buf;
		f->buf = tmp;
		f->buf_size = sizeof tmp;
		f->wpos = f->wbase = f->wend = 0;
	}
	va_copy(ap2, ap);
	if (!f->wend && __towrite(f))
		ret = -1;
	else
		ret = printf_core(f, fmt, &ap2, nl_arg, nl_type, saved_errno);
	va_end(ap2);
	if (saved) {
		if (f->wpos != f->wbase)
			f_write(f, 0, 0);
		f->buf = saved;
		f->buf_size = 0;
		f->wpos = f->wbase = f->wend = 0;
	}
	if (f->flags & F_ERR)
		ret = -1;
	f->flags |= olderr;
	return ret;
}

int vfprintf(FILE *__restrict f, const char *__restrict fmt, va_list ap)
{
	FLOCK(f);
	int r = __vfprintf_unlocked(f, fmt, ap);
	FUNLOCK(f);
	return r;
}
