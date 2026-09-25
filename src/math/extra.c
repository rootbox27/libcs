/* Remaining C99 functions: scalbln, nexttoward, nanl, the ilogb/lrint/
 * lround variants, and an exact fmal. */
#include "libm.h"
#include <limits.h>

static int clamp_int(long n) { return n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n; }

double scalbln(double x, long n) { return scalbn(x, clamp_int(n)); }
float scalblnf(float x, long n) { return scalbnf(x, clamp_int(n)); }
long double scalblnl(long double x, long n) { return scalbnl(x, clamp_int(n)); }

/* nextafter towards a long double: y may lie strictly between x and its
 * neighbour, so only the direction is taken from it */
double nexttoward(double x, long double y)
{
	if (x != x || y != y)
		return (double)((long double)x + y);
	if (x < y)
		return nextafter(x, INFINITY);
	if (x > y)
		return nextafter(x, -INFINITY);
	return (double)y;
}

float nexttowardf(float x, long double y)
{
	if (x != x || y != y)
		return (float)((long double)x + y);
	if (x < y)
		return nextafterf(x, INFINITY);
	if (x > y)
		return nextafterf(x, -INFINITY);
	return (float)y;
}

long double nexttowardl(long double x, long double y) { return nextafterl(x, y); }

long double nanl(const char *s) { (void)s; return __builtin_nanl(""); }

int ilogbf(float x) { return ilogb(x); }

int ilogbl(long double x)
{
	if (x != x || x == 0) {
		force_eval(math_invalid(0));
		return INT_MIN;
	}
	if (isinf(x)) {
		force_eval(math_invalid(0));
		return INT_MAX;
	}
	int e;
	frexpl(x, &e);
	return e - 1;
}

long lrintl(long double x)
{
	long r;
	__asm__("fistpll %0" : "=m"(r) : "t"(x) : "st");
	return r;
}
long long llrintl(long double x) { return lrintl(x); }
long long llrintf(float x) { return lrintf(x); }

long lroundl(long double x) { return lrintl(roundl(x)); }
long long llroundl(long double x) { return lroundl(x); }
long long llroundf(float x) { return lroundf(x); }

/* ---- fmal: x*y + z with one rounding ---- */

typedef struct { uint64_t w[3]; } u192; /* w[2] most significant */

static u192 shl192(u192 a, int n)
{
	u192 r = { { 0, 0, 0 } };
	int ws = n / 64, bs = n % 64;
	for (int i = 2; i >= ws; i--) {
		uint64_t v = a.w[i - ws] << bs;
		if (bs && i - ws - 1 >= 0)
			v |= a.w[i - ws - 1] >> (64 - bs);
		r.w[i] = v;
	}
	return r;
}

/* a >> n with the shifted-out bits ORed into bit 0 */
static u192 shr192_sticky(u192 a, int n)
{
	if (n <= 0)
		return a;
	u192 r = { { 0, 0, 0 } };
	if (n >= 192) {
		r.w[0] = (a.w[0] | a.w[1] | a.w[2]) != 0;
		return r;
	}
	int ws = n / 64, bs = n % 64;
	uint64_t lost = 0;
	for (int i = 0; i < ws; i++)
		lost |= a.w[i];
	if (bs)
		lost |= a.w[ws] << (64 - bs);
	for (int i = 0; i + ws <= 2; i++) {
		uint64_t v = a.w[i + ws] >> bs;
		if (bs && i + ws + 1 <= 2)
			v |= a.w[i + ws + 1] << (64 - bs);
		r.w[i] = v;
	}
	r.w[0] |= lost != 0;
	return r;
}

static int cmp192(u192 a, u192 b)
{
	for (int i = 2; i >= 0; i--)
		if (a.w[i] != b.w[i])
			return a.w[i] < b.w[i] ? -1 : 1;
	return 0;
}

static u192 add192(u192 a, u192 b)
{
	u192 r;
	unsigned __int128 c = 0;
	for (int i = 0; i < 3; i++) {
		c += (unsigned __int128)a.w[i] + b.w[i];
		r.w[i] = (uint64_t)c;
		c >>= 64;
	}
	return r;
}

static u192 sub192(u192 a, u192 b) /* a >= b */
{
	u192 r;
	uint64_t borrow = 0;
	for (int i = 0; i < 3; i++) {
		uint64_t d = a.w[i] - b.w[i];
		uint64_t b2 = a.w[i] < b.w[i] || d < borrow;
		r.w[i] = d - borrow;
		borrow = b2;
	}
	return r;
}

static int top192(u192 a) /* index of the highest set bit; a != 0 */
{
	for (int i = 2; i >= 0; i--)
		if (a.w[i])
			return 64 * i + 63 - __builtin_clzll(a.w[i]);
	return -1;
}

/* m 2^e for finite nonzero x, m with bit 63 set */
static uint64_t unpackl(long double x, int *e)
{
	union ldbits u = { x };
	int be = u.i.se & 0x7fff;
	uint64_t m = u.i.m;
	if (be == 0)
		be = 1; /* denormal: same scale as the smallest normal */
	int s = __builtin_clzll(m);
	*e = be - 16383 - 63 - s;
	return m << s;
}

static long double opaquel(long double x) { __asm__("" : "+t"(x)); return x; }

long double fmal(long double x, long double y, long double z)
{
	if (!isfinite(x) || !isfinite(y) || x == 0 || y == 0)
		return x * y + z;
	if (!isfinite(z))
		return z + z;
	if (z == 0)
		return x * y;

	int ex, ey, ez;
	uint64_t mx = unpackl(x, &ex), my = unpackl(y, &ey), mz = unpackl(z, &ez);
	int sa = signbit(x) != signbit(y), sb = signbit(z) != 0;
	/* the product (127-128 bits) and z (64 bits) as 192-bit integers with
	 * the top bit at 188-189, times 2^ae and 2^be */
	unsigned __int128 p = (unsigned __int128)mx * my;
	u192 A = { { (uint64_t)p, (uint64_t)(p >> 64), 0 } };
	A = shl192(A, 62);
	int ae = ex + ey - 62;
	u192 B = { { mz, 0, 0 } };
	B = shl192(B, 125);
	int be = ez - 125;
	if (be > ae) {
		u192 t = A; A = B; B = t;
		int ti = ae; ae = be; be = ti;
		ti = sa; sa = sb; sb = ti;
	}
	B = shr192_sticky(B, ae - be);
	u192 S;
	int neg;
	if (sa == sb) {
		S = add192(A, B);
		neg = sa;
	} else if (cmp192(A, B) >= 0) {
		S = sub192(A, B);
		neg = sa;
	} else {
		S = sub192(B, A);
		neg = sb;
	}
	if (!(S.w[0] | S.w[1] | S.w[2]))
		return opaquel(0.0L) - opaquel(0.0L); /* -0 when rounding down */
	int t = top192(S);
	int emsb = t + ae;
	if (emsb > 16383)
		return (neg ? -0x1p16383L : 0x1p16383L) * opaquel(2.0L);
	/* keep k significant bits (fewer when the result is subnormal) */
	int k = emsb >= -16382 ? 64 : 64 - (-16382 - emsb);
	int sh = t - k + 1; /* bits below the kept ones */
	u192 G = sh >= 2 ? shr192_sticky(S, sh - 2) : shl192(S, 2 - sh);
	uint64_t H = k > 0 ? G.w[0] >> 2 | G.w[1] << 62 : 0;
	int g = (int)(G.w[0] >> 1 & 1), st = (int)(G.w[0] & 1);
	if (k <= 0) {
		/* below the smallest subnormal: only the rounding bits matter */
		g = k == 0 ? g : 0;
		st = st | (k < 0);
		H = 0;
	}
	/* round at integer precision: offset so the sum lies in [2^63, 2^64] */
	long double off = k < 64 ? 0x1p63L : 0.0L;
	long double frac = g * 0.5L + st * 0.25L;
	long double base = off + (long double)H; /* exact */
	long double r = neg ? -base - frac : base + frac;
	r = neg ? r + off : r - off; /* exact */
	if (r == 0)
		r = neg ? -0.0L : 0.0L; /* rounded away entirely: keep the sign */
	int scale = emsb - k + 1; /* weight of H's lowest bit */
	if (k < 64 && (g | st))
		force_eval(math_uflow(0));
	if (scale < -16382)
		return r * 0x1p-8000L * scalbnl(1.0L, scale + 8000);
	return scalbnl(r, scale);
}
