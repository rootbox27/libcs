/* Floating-point parsing: strtod, strtof, strtold, atof.
 *
 * Results are correctly rounded (to nearest, ties to even). Decimal input
 * is converted exactly with big-integer division; up to MAX_DIGITS
 * significant digits are kept exactly and any further nonzero digits act
 * as a sticky bit. That is exact for float and double (a halfway point
 * never needs more than 767 digits); for long double, inputs of more than
 * MAX_DIGITS significant digits lying within 10^-MAX_DIGITS (relative) of
 * a halfway point could in principle round the wrong way. */
#include "internal.h"
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIGITS 800

typedef unsigned __int128 u128;

struct ffmt {
	int p;      /* mantissa bits */
	int emin;   /* exponent of the least significant bit of subnormals */
	int emax;   /* largest exponent of the LSB of a normal p-bit mantissa */
	int dmax;   /* values >= 10^dmax overflow */
	int dmin;   /* values < 10^dmin underflow to zero */
};

static const struct ffmt F_FLT = { 24, -149, 104, 40, -47 };
static const struct ffmt F_DBL = { 53, -1074, 971, 310, -325 };
static const struct ffmt F_LDBL = { 64, -16445, 16320, 4934, -4953 };

/* ---- result assembly ---- */

/* value = M * 2^E, M < 2^64, as an x87 long double (exact). */
static long double make_ld(uint64_t M, int E, int neg)
{
	union {
		long double f;
		struct { uint64_t m; uint16_t se; } i;
	} u = { .f = 0 };
	if (M) {
		int s = __builtin_clzll(M);
		int field = E - s + 63 + 16383;
		if (field >= 1) {
			u.i.m = M << s;
			u.i.se = (uint16_t)field;
		} else {
			/* long double subnormal: E is -16445 here */
			u.i.m = M;
			u.i.se = 0;
		}
	}
	if (neg)
		u.i.se |= 0x8000;
	return u.f;
}

static long double make_inf(int neg)
{
	union {
		long double f;
		struct { uint64_t m; uint16_t se; } i;
	} u;
	u.i.m = 1ULL << 63;
	u.i.se = (uint16_t)(0x7fff | (neg ? 0x8000 : 0));
	return u.f;
}

static long double make_nan(int neg)
{
	union {
		long double f;
		struct { uint64_t m; uint16_t se; } i;
	} u;
	u.i.m = 3ULL << 62;
	u.i.se = (uint16_t)(0x7fff | (neg ? 0x8000 : 0));
	return u.f;
}

/* Round Q * 2^E (plus a sticky amount below Q's last bit) to the format,
 * store the value and set errno on overflow or inexact underflow. */
static long double round_to(u128 Q, int E, int sticky, const struct ffmt *F, int neg)
{
	if (!Q)
		return make_ld(0, 0, neg);
	int bl = 128 - (int)((uint64_t)(Q >> 64) ? __builtin_clzll((uint64_t)(Q >> 64)) : 64 + __builtin_clzll((uint64_t)Q));
	int drop = bl - F->p;
	if (E + drop < F->emin)
		drop = F->emin - E;
	uint64_t keep;
	int inexact = sticky;
	if (drop > 0) {
		u128 k = drop >= 128 ? 0 : Q >> drop;
		u128 rem = drop >= 128 ? Q : Q & (((u128)1 << drop) - 1);
		int up = 0;
		if (rem || sticky) {
			inexact = 1;
			if (drop <= 128) {
				u128 half = (u128)1 << (drop - 1);
				up = rem > half || (rem == half && (sticky || (k & 1)));
			}
		}
		k += up;
		E += drop;
		if (k >> F->p) { /* carry to a new bit */
			k >>= 1;
			E++;
		}
		keep = (uint64_t)k;
	} else {
		keep = (uint64_t)(Q << -drop);
		E += drop;
	}
	if (keep && E > F->emax) {
		errno = ERANGE;
		return make_inf(neg);
	}
	if (inexact && !(keep >> (F->p - 1)))
		errno = ERANGE; /* tiny and inexact */
	return make_ld(keep, E, neg);
}

/* ---- big integers (base 2^32, least significant limb first) ---- */

#define BL 720

struct big {
	int n;
	uint32_t d[BL];
};

static void big_small(struct big *b, uint32_t v)
{
	b->n = v ? 1 : 0;
	b->d[0] = v;
}

static void big_muladd(struct big *b, uint32_t m, uint32_t a)
{
	uint64_t c = a;
	for (int i = 0; i < b->n; i++) {
		uint64_t t = (uint64_t)b->d[i] * m + c;
		b->d[i] = (uint32_t)t;
		c = t >> 32;
	}
	if (c)
		b->d[b->n++] = (uint32_t)c;
}

static void big_mulpow10(struct big *b, int k)
{
	for (; k >= 9; k -= 9)
		big_muladd(b, 1000000000u, 0);
	static const uint32_t p10[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };
	if (k)
		big_muladd(b, p10[k], 0);
}

static void big_shl(struct big *b, int k)
{
	if (!b->n || !k)
		return;
	int w = k / 32, s = k % 32;
	if (s) {
		uint32_t c = 0;
		for (int i = 0; i < b->n; i++) {
			uint32_t t = b->d[i];
			b->d[i] = t << s | c;
			c = t >> (32 - s);
		}
		if (c)
			b->d[b->n++] = c;
	}
	if (w) {
		memmove(b->d + w, b->d, (size_t)b->n * 4);
		memset(b->d, 0, (size_t)w * 4);
		b->n += w;
	}
}

static int big_bits(const struct big *b)
{
	return b->n ? 32 * (b->n - 1) + 32 - __builtin_clz(b->d[b->n - 1]) : 0;
}

static int big_cmp(const struct big *a, const struct big *b)
{
	if (a->n != b->n)
		return a->n < b->n ? -1 : 1;
	for (int i = a->n - 1; i >= 0; i--)
		if (a->d[i] != b->d[i])
			return a->d[i] < b->d[i] ? -1 : 1;
	return 0;
}

static void big_sub(struct big *a, const struct big *b)
{
	int64_t c = 0;
	for (int i = 0; i < a->n; i++) {
		int64_t t = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) + c;
		a->d[i] = (uint32_t)t;
		c = t >> 32;
	}
	while (a->n && !a->d[a->n - 1])
		a->n--;
}

/* The top `want` (<= 128) bits of b as an integer; *shift receives
 * bits(b) - want, so b ~= result * 2^shift (truncated). */
static u128 big_top(const struct big *b, int want, int *shift)
{
	int nb = big_bits(b);
	*shift = nb - want;
	u128 r = 0;
	for (int bit = nb - 1; bit >= 0 && bit >= nb - want; ) {
		/* take the rest of the limb containing `bit` */
		int limb = bit / 32, lo = limb * 32;
		int take = bit - lo + 1;
		int need = want - (nb - 1 - bit);
		uint32_t v = b->d[limb];
		if (take > need) {
			v >>= take - need;
			take = need;
		}
		r = r << take | (v & (take == 32 ? 0xffffffffu : (1u << take) - 1));
		bit -= take;
	}
	if (nb < want)
		r <<= want - nb;
	return r;
}

/* r = a * m */
static void big_mul128(struct big *r, const struct big *a, u128 m)
{
	uint32_t md[4];
	int mn = 0;
	for (; m; m >>= 32)
		md[mn++] = (uint32_t)m;
	int n = a->n + mn;
	memset(r->d, 0, (size_t)n * 4);
	for (int j = 0; j < mn; j++) {
		uint64_t c = 0;
		for (int i = 0; i < a->n; i++) {
			uint64_t t = (uint64_t)a->d[i] * md[j] + r->d[i + j] + c;
			r->d[i + j] = (uint32_t)t;
			c = t >> 32;
		}
		r->d[a->n + j] = (uint32_t)c;
	}
	r->n = n;
	while (r->n && !r->d[r->n - 1])
		r->n--;
}

/* ---- decimal ---- */

static const double exact10[] = {
	1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
	1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

/* digits: nd significant digits (no leading zeros), value digits * 10^q */
static long double decimal(const char *digits, int nd, long q, int sticky, const struct ffmt *F, int neg)
{
	if (!nd)
		return make_ld(0, 0, neg);
	if (q + nd > F->dmax) {
		errno = ERANGE;
		return make_inf(neg);
	}
	if (q + nd < F->dmin) {
		errno = ERANGE;
		return make_ld(0, 0, neg);
	}

	/* Fast path (Clinger): both operands exact in double, so one IEEE
	 * operation rounds correctly. */
	if (F == &F_DBL && nd <= 15 && !sticky && q >= -22 && q <= 22) {
		uint64_t v = 0;
		for (int i = 0; i < nd; i++)
			v = v * 10 + (uint64_t)(digits[i] - '0');
		volatile double d = (double)v;
		d = q < 0 ? d / exact10[-q] : d * exact10[q];
		double r = d;
		return neg ? -(long double)r : (long double)r;
	}

	/* Middle path for float and double: <= 19 digits and 10^|q| are exact
	 * in long double, so v * 10^q has a single rounding to 64 bits. Rounding
	 * that on to the target is then correct unless the 64-bit result lies
	 * exactly on a target halfway point; those take the exact path. */
	if (F != &F_LDBL && nd <= 19 && !sticky && q >= -27 && q <= 27) {
		static const long double p10l[28] = {
			1e0L, 1e1L, 1e2L, 1e3L, 1e4L, 1e5L, 1e6L, 1e7L, 1e8L, 1e9L,
			1e10L, 1e11L, 1e12L, 1e13L, 1e14L, 1e15L, 1e16L, 1e17L, 1e18L,
			1e19L, 1e20L, 1e21L, 1e22L, 1e23L, 1e24L, 1e25L, 1e26L, 1e27L,
		};
		uint64_t v = 0;
		for (int i = 0; i < nd; i++)
			v = v * 10 + (uint64_t)(digits[i] - '0');
		volatile long double r = (long double)v;
		r = q < 0 ? r / p10l[-q] : r * p10l[q];
		union {
			long double f;
			struct { uint64_t m; uint16_t se; } i;
		} u = { .f = r };
		int drop = 64 - F->p;
		uint64_t low = u.i.m & ((1ULL << drop) - 1);
		/* unbiased exponent of the leading bit, must be normal in target */
		int X = (u.i.se & 0x7fff) - 16383;
		int maxX = F->emax + F->p - 1, minX = F->emin + F->p - 1;
		if (low != 1ULL << (drop - 1) && X <= maxX && X >= minX) {
			long double res = F == &F_DBL ? (long double)(double)r : (long double)(float)r;
			return neg ? -res : res;
		}
	}

	struct big num, den, t;
	big_small(&num, 0);
	for (int i = 0; i < nd; i += 9) {
		uint32_t chunk = 0, mul = 1;
		for (int j = i; j < nd && j < i + 9; j++) {
			chunk = chunk * 10 + (uint32_t)(digits[j] - '0');
			mul *= 10;
		}
		if (!num.n)
			big_small(&num, chunk);
		else
			big_muladd(&num, mul, chunk);
	}
	big_small(&den, 1);
	if (q >= 0)
		big_mulpow10(&num, (int)q);
	else
		big_mulpow10(&den, (int)-q);

	/* Scale so the quotient has p+2 or p+3 bits. */
	int e = big_bits(&num) - big_bits(&den) - F->p - 2;
	if (e < 0)
		big_shl(&num, -e);
	else
		big_shl(&den, e);

	/* Q = floor(num / den), Q < 2^(p+3) <= 2^67. Estimate from the top
	 * bits with a divisor rounded up, which can only undershoot, then
	 * subtract Q*den and correct upwards. */
	int sn, sd;
	u128 N = big_top(&num, 128, &sn);
	u128 D = big_top(&den, 64, &sd);
	u128 Q = N / (D + 1);
	int sh = sn - sd;
	Q = sh >= 0 ? Q << sh : Q >> -sh;
	big_mul128(&t, &den, Q);
	big_sub(&num, &t);
	while (big_cmp(&num, &den) >= 0) {
		big_sub(&num, &den);
		Q++;
	}
	return round_to(Q, e, sticky || num.n, F, neg);
}

/* ---- the parser ---- */

static int ci_prefix(const char *s, const char *word)
{
	size_t i = 0;
	for (; word[i]; i++)
		if ((s[i] | 32) != word[i])
			return 0;
	return (int)i;
}

static long double parse(const char *s0, char **end, const struct ffmt *F)
{
	const char *s = s0;
	while (isspace((unsigned char)*s))
		s++;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';

	int k;
	if ((k = ci_prefix(s, "inf"))) {
		s += k;
		if ((k = ci_prefix(s, "inity")))
			s += k;
		if (end)
			*end = (char *)s;
		return make_inf(neg);
	}
	if ((k = ci_prefix(s, "nan"))) {
		s += k;
		if (*s == '(') {
			const char *p = s + 1;
			while (isalnum((unsigned char)*p) || *p == '_')
				p++;
			if (*p == ')')
				s = p + 1;
		}
		if (end)
			*end = (char *)s;
		return make_nan(neg);
	}

	if (s[0] == '0' && (s[1] | 32) == 'x' &&
	    (isxdigit((unsigned char)s[2]) || (s[2] == '.' && isxdigit((unsigned char)s[3])))) {
		/* hexadecimal: keep 124 significant bits, the rest is sticky */
		s += 2;
		u128 Q = 0;
		int bits = 0, sticky = 0, dot = 0;
		long e = 0;
		for (;; s++) {
			int d;
			if (*s == '.' && !dot) {
				dot = 1;
				continue;
			}
			if ((unsigned)*s - '0' < 10)
				d = *s - '0';
			else if ((unsigned)(*s | 32) - 'a' < 6)
				d = (*s | 32) - 'a' + 10;
			else
				break;
			if (!Q && !d) {
				if (dot)
					e -= 4;
				continue;
			}
			if (bits < 124) {
				Q = Q << 4 | (unsigned)d;
				bits += 4;
				if (dot)
					e -= 4;
			} else {
				sticky |= d != 0;
				if (!dot)
					e += 4;
			}
		}
		if ((*s | 32) == 'p') {
			const char *p = s + 1;
			int eneg = 0;
			if (*p == '+' || *p == '-')
				eneg = *p++ == '-';
			if ((unsigned)*p - '0' < 10) {
				long x = 0;
				for (; (unsigned)*p - '0' < 10; p++)
					if (x < 100000000)
						x = x * 10 + (*p - '0');
				e += eneg ? -x : x;
				s = p;
			}
		}
		if (end)
			*end = (char *)s;
		if (e > 100000) {
			if (!Q)
				return make_ld(0, 0, neg);
			errno = ERANGE;
			return make_inf(neg);
		}
		if (e < -100000) {
			if (Q)
				errno = ERANGE;
			return make_ld(0, 0, neg);
		}
		return round_to(Q, (int)e, sticky, F, neg);
	}

	/* decimal */
	char digits[MAX_DIGITS];
	int nd = 0, sticky = 0, dot = 0, any = 0;
	long q = 0;
	for (;; s++) {
		if (*s == '.' && !dot) {
			dot = 1;
			continue;
		}
		if ((unsigned)*s - '0' >= 10)
			break;
		any = 1;
		if (*s == '0' && !nd) {
			if (dot)
				q--;
			continue;
		}
		if (nd < MAX_DIGITS) {
			digits[nd++] = *s;
			if (dot)
				q--;
		} else {
			sticky |= *s != '0';
			if (!dot)
				q++;
		}
	}
	if (!any) {
		if (end)
			*end = (char *)s0;
		return 0;
	}
	if ((*s | 32) == 'e') {
		const char *p = s + 1;
		int eneg = 0;
		if (*p == '+' || *p == '-')
			eneg = *p++ == '-';
		if ((unsigned)*p - '0' < 10) {
			long x = 0;
			for (; (unsigned)*p - '0' < 10; p++)
				if (x < 100000000)
					x = x * 10 + (*p - '0');
			q += eneg ? -x : x;
			s = p;
		}
	}
	if (end)
		*end = (char *)s;
	/* drop trailing zeros of the significand */
	while (nd && digits[nd - 1] == '0') {
		nd--;
		q++;
	}
	return decimal(digits, nd, q, sticky, F, neg);
}

long double strtold(const char *__restrict s, char **__restrict end)
{
	return parse(s, end, &F_LDBL);
}

double strtod(const char *__restrict s, char **__restrict end)
{
	return (double)parse(s, end, &F_DBL);
}

float strtof(const char *__restrict s, char **__restrict end)
{
	return (float)parse(s, end, &F_FLT);
}

double atof(const char *s)
{
	return strtod(s, 0);
}
