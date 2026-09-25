/* cbrt, hypot, fma, fmaf. */
#include "libm.h"



double cbrt(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	if (ax >= 0x7ff0000000000000ULL || ax == 0)
		return x + x;
	int adj = 0;
	if (ax < 0x0010000000000000ULL) {
		ax = asu64(asdbl(ax) * 0x1p54);
		adj = -18;
	}
	/* |x| = m 2^(3q) with m in [1, 8) */
	int e = (int)(ax >> 52) - 0x3ff;
	int q = (e >= 0 ? e : e - 2) / 3;
	int rem = e - 3 * q;
	double m = asdbl((ax & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL) * (double)(1 << rem);
	/* first guess from the bits (a few percent), then Newton */
	double y = asdbl((asu64(m) - 0x3ff0000000000000ULL) / 3 + 0x3ff0000000000000ULL);
	for (int i = 0; i < 4; i++)
		y = y - (y * y * y - m) / (3 * y * y);
	/* one more step with y^3 - m computed exactly */
	double pe;
	double p = two_prod(y, y, &pe);
	double ce;
	double c = two_prod(p, y, &ce);
	ce += pe * y;
	double r = (c - m) + ce;
	double res = y - r / (3 * p);
	res *= asdbl((uint64_t)(0x3ff + q + adj) << 52);
	return (ix >> 63) ? -res : res;
}

/* (hi + lo) 2^k rounded once, also when the result is subnormal;
 * hi + lo must be normal and at least 2^-1000 or so. */
hidden double __scale_dd(double hi, double lo, int k)
{
	double y = hi + lo;
	int e = (int)(asu64(y) >> 52 & 0x7ff) - 0x3ff;
	if (e + k >= -1022) {
		if (k > 1000)
			return y * 0x1p1000 * asdbl((uint64_t)(0x3ff + k - 1000) << 52);
		return y * asdbl((uint64_t)(0x3ff + k) << 52);
	}
	/* subnormal: in units where the ulp is 2^-52, round once by adding 1 */
	double s = asdbl((uint64_t)(0x3ff + k + 1022) << 52);
	double a = hi * s, b = lo * s;
	double te;
	double t = two_sum(1.0, a, &te);
	double r = t + (te + b);
	if (r - t != te + b)
		force_eval(math_uflow(0)); /* inexact and tiny */
	return (r - 1.0) * 0x1p-1022;
}

double hypot(double x, double y)
{
	double ax = fabs(x), ay = fabs(y);
	if (isinf(ax) || isinf(ay))
		return INFINITY;
	if (ax != ax || ay != ay)
		return x + y;
	if (ay > ax) {
		double t = ax;
		ax = ay;
		ay = t;
	}
	if (ay == 0)
		return ax;
	int ea = (int)(asu64(ax) >> 52), eb = (int)(asu64(ay) >> 52);
	if (ea - eb > 60)
		return ax + ay; /* ay^2 is far below half an ulp of ax^2 */
	int k = 0;
	if (ea > 0x3ff + 500) {
		ax *= 0x1p-600;
		ay *= 0x1p-600;
		k = 600;
	} else if (eb < 0x3ff - 500) {
		ax *= 0x1p600;
		ay *= 0x1p600;
		k = -600;
	}
	double e1, e2, e3;
	double p1 = two_prod(ax, ax, &e1);
	double p2 = two_prod(ay, ay, &e2);
	double s = fast_two_sum(p1, p2, &e3);
	double sl = e3 + e1 + e2;
	double h = sqrt_(s);
	double he;
	double hh = two_prod(h, h, &he);
	double corr = (((s - hh) - he) + sl) / (2 * h);
	if (k == 0)
		return h + corr;
	return __scale_dd(h, corr, k);
}

/* ---- fused multiply-add ---- */

/* m 2^e with m in [2^52, 2^53), for finite nonzero x */
static uint64_t unpack(double x, int *e)
{
	uint64_t ix = asu64(x) & 0x7fffffffffffffffULL;
	int ex = (int)(ix >> 52);
	uint64_t m = ix & 0x000fffffffffffffULL;
	if (ex == 0) {
		int sh = __builtin_clzll(m) - 11;
		*e = -1074 - sh;
		return m << sh;
	}
	*e = ex - 1075;
	return m | 0x0010000000000000ULL;
}

static int clz128(u128 v)
{
	uint64_t hi = (uint64_t)(v >> 64);
	return hi ? __builtin_clzll(hi) : 64 + __builtin_clzll((uint64_t)v);
}

/* v >> n with the shifted-out bits ORed into bit 0 */
static u128 shr_sticky(u128 v, int n)
{
	if (n <= 0)
		return v;
	if (n >= 128)
		return v != 0;
	return (v >> n) | ((v & (((u128)1 << n) - 1)) != 0);
}

double fma(double x, double y, double z)
{
	if (!isfinite(x) || !isfinite(y) || x == 0 || y == 0)
		return x * y + z;
	if (!isfinite(z))
		return z + z;
	if (z == 0)
		return x * y; /* one rounding of the exact product */

	int ex, ey, ez;
	uint64_t mx = unpack(x, &ex), my = unpack(y, &ey), mz = unpack(z, &ez);
	int sa = (int)((asu64(x) ^ asu64(y)) >> 63), sb = (int)(asu64(z) >> 63);
	/* Both operands as 128-bit integers with the top bit at 124 or 125
	 * (the product has at most 106 significant bits, z 53), times 2^ae
	 * and 2^be. Bits shifted out below are only lost when the other
	 * operand is much larger, so a sticky bit suffices. */
	u128 A = ((u128)mx * my) << 20, B = (u128)mz << 72;
	int ae = ex + ey - 20, be = ez - 72;
	if (be > ae) {
		u128 t = A; A = B; B = t;
		int ti = ae; ae = be; be = ti;
		ti = sa; sa = sb; sb = ti;
	}
	B = shr_sticky(B, ae - be);
	u128 S;
	int neg;
	if (sa == sb) {
		S = A + B;
		neg = sa;
	} else if (A >= B) {
		S = A - B;
		neg = sa;
	} else {
		S = B - A;
		neg = sb;
	}
	if (S == 0)
		return opaque(0.0) - opaque(0.0); /* -0 when rounding down */
	int t = 127 - clz128(S);
	int emsb = t + ae; /* S 2^ae = 1.xxx 2^emsb */
	if (emsb > 1023)
		return (neg ? -0x1p1023 : 0x1p1023) * opaque(2.0);
	if (emsb >= -1022) {
		/* 63 significant bits with sticky; the conversion rounds once in
		 * the current mode */
		uint64_t m = t > 62 ? (uint64_t)shr_sticky(S, t - 62) : (uint64_t)(S << (62 - t));
		double r = (double)(neg ? -(int64_t)m : (int64_t)m);
		return r * 0x1p-62 * asdbl((uint64_t)(0x3ff + emsb) << 52);
	}
	/* subnormal: sh bits of S lie below 2^-1074 */
	int sh = -1074 - ae;
	if (sh <= 0) {
		double r = (double)(uint64_t)(S << -sh) * 0x1p-1074; /* exact */
		return neg ? -r : r;
	}
	/* keep guard and sticky bits: G = M.gs */
	u128 G = sh >= 2 ? shr_sticky(S, sh - 2) : S << 1;
	uint64_t M = (uint64_t)(G >> 2);
	double add = (double)(int)((G >> 1) & 1) * 0x1p-53 + (double)(int)(G & 1) * 0x1p-54;
	double one = neg ? -1.0 : 1.0;
	double base = one + (neg ? -(double)M : (double)M) * 0x1p-52; /* exact */
	double r = base + (neg ? -add : add);
	if (G & 3)
		force_eval(math_uflow(0));
	double v = (r - one) * 0x1p-1022;
	return v == 0 ? (neg ? -0.0 : 0.0) : v; /* a rounded-away result keeps its sign */
}

float fmaf(float x, float y, float z)
{
	double p = (double)x * y; /* exact */
	double zd = z;
	if (!isfinite(p) || !isfinite(zd))
		return (float)(p + zd);
	/* p + z rounded to odd in double, then to float: a single rounding */
	unsigned mx = get_mxcsr();
	set_mxcsr(mx & ~0x6000u); /* round to nearest; restoring drops flags */
	double e;
	double s = two_sum(p, zd, &e);
	set_mxcsr(mx);
	if (e == 0)
		return (float)(p + zd); /* exact in double: current mode applies */
	if (!(asu64(s) & 1))
		s = asdbl(asu64(s) + ((e > 0) == (s > 0) ? 1 : -1));
	return (float)s;
}
