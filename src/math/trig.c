/* sin, cos, tan.
 *
 * Argument reduction: x = n pi/2 + y with y as a double-double. For
 * |x| < 2^20 pi/2, pi/2 is split into three 33-bit pieces and a tail, so
 * n times each piece is exact. Larger arguments use Payne-Hanek with
 * 1280 bits of 2/pi. The kernels on [-pi/4, pi/4] form their leading
 * terms in double-double. */
#include "libm.h"
#include "tables.h"

#define NW 5 /* words of 2/pi used per reduction */

/* Payne-Hanek: for x = m 2^(e - mbits + 1) (m with mbits significant bits,
 * top bit set), x 2/pi = n + f 2^-128 - neg... precisely: returns n mod 4
 * and sets *fp to |x 2/pi - n| in units of 2^-128 (at most 2^127) and
 * *negp if x 2/pi - n < 0. Words of 2/pi whose products are multiples of
 * 4 are skipped; the NW words used leave an error below 2^-190. */
hidden int __rem_pio2_bits(uint64_t m, int mbits, int e, u128 *fp, int *negp)
{
	int skip = e - mbits - 1;
	int k0 = skip > 0 ? skip / 64 : 0;
	/* (NW+1)-word product m * (w[k0] .. w[k0+NW-1]); acc[0] most significant */
	uint64_t acc[NW + 1] = { 0 };
	for (int j = NW - 1; j >= 0; j--) {
		u128 p = (u128)m * two_over_pi[k0 + j];
		/* add p at word position j (occupies acc[j], acc[j+1]) */
		u128 lo = (u128)acc[j + 1] + (uint64_t)p;
		acc[j + 1] = (uint64_t)lo;
		u128 hi = (u128)acc[j] + (uint64_t)(p >> 64) + (uint64_t)(lo >> 64);
		acc[j] = (uint64_t)hi;
		for (int c = j - 1; c >= 0 && (hi >> 64); c--) {
			hi = (u128)acc[c] + 1;
			acc[c] = (uint64_t)hi;
		}
	}
	/* position of the binary point counted from bit 0 of acc[NW] */
	int point = (mbits - 1) + 64 * (k0 + NW) - e;
	/* a 192-bit window with the binary point at its bit 128 */
	int shift = point - 128;
	uint64_t win[3];
	for (int i = 0; i < 3; i++) {
		int b = shift + 64 * i;
		int wi = b / 64, wb = b % 64;
		uint64_t lo_w = wi <= NW ? acc[NW - wi] : 0;
		uint64_t hi_w = wi + 1 <= NW ? acc[NW - 1 - wi] : 0;
		win[i] = wb ? (lo_w >> wb) | (hi_w << (64 - wb)) : lo_w;
	}
	int n = (int)(win[2] & 3);
	u128 f = (u128)win[1] << 64 | win[0];
	*negp = 0;
	if (f >> 127) {
		/* fraction >= 1/2: round n up and use the negative remainder */
		n++;
		f = -f;
		*negp = 1;
	}
	*fp = f;
	return n & 3;
}

/* x (finite, |x| >= 2^20 pi/2 or so) reduced mod pi/2 */
static int rem_pio2_large(double x, double *y)
{
	uint64_t ix = asu64(x);
	int e = (int)(ix >> 52 & 0x7ff) - 0x3ff;
	uint64_t m = (ix & ((1ULL << 52) - 1)) | (1ULL << 52);
	u128 f;
	int neg;
	int n = __rem_pio2_bits(m, 53, e, &f, &neg);
	/* y = f 2^-128 pi/2; f 2^-128 = fh + fl with fh the top word rounded */
	uint64_t top = (uint64_t)(f >> 64); /* <= 2^63 here, so no overflow */
	double part = (double)top;
	double perr = (double)(int64_t)(top - (uint64_t)part);
	double fh = part * 0x1p-64;
	double fl = (perr + (double)(uint64_t)f * 0x1p-64) * 0x1p-64;
	double pe;
	double p = two_prod(fh, PIO2_HI, &pe);
	double lo = pe + fh * PIO2_LO + fl * PIO2_HI;
	double r = fast_two_sum(p, lo, &y[1]);
	y[0] = r;
	if (neg) {
		y[0] = -y[0];
		y[1] = -y[1];
	}
	return n;
}

hidden int __rem_pio2(double x, double *y)
{
	double ax = fabs(x);
	if (ax <= PIO4_HI) {
		y[0] = x;
		y[1] = 0;
		return 0;
	}
	int n;
	if (ax < 0x1p20 * PIO2_HI) {
		/* round to nearest independent of the rounding mode; being off
		 * by one at a tie only moves y slightly past pi/4 */
		n = (int)(x * INVPIO2 + (x < 0 ? -0.5 : 0.5));
		double fn = n;
		double a = x - fn * PIO2_1;        /* exact */
		double e1, e2;
		double s = two_sum(a, -fn * PIO2_2, &e1);
		s = two_sum(s, -fn * PIO2_3, &e2);
		double lo = (e1 + e2) - fn * PIO2_3T;
		y[0] = fast_two_sum(s, lo, &y[1]);
		return n;
	}
	n = rem_pio2_large(ax, y);
	if (x < 0) {
		y[0] = -y[0];
		y[1] = -y[1];
		n = -n;
	}
	return n;
}

/* sin(x + y), |x| <= pi/4, |y| tiny */
hidden double __sin_k(double x, double y)
{
	double ze;
	double z = two_prod(x, x, &ze);
	double x3e;
	double x3 = two_prod(z, x, &x3e);
	x3e += ze * x;
	/* x - x^3/6 in double-double, then x^5 P(z) */
	double t0e;
	double t0 = two_prod(x3, -SIXTH_HI, &t0e);
	t0e -= x3e * SIXTH_HI + x3 * SIXTH_LO;
	double rest = x3 * z * (SIN_C0 + z * (SIN_C1 + z * (SIN_C2 + z * (SIN_C3 + z * (SIN_C4 + z * SIN_C5)))));
	double e1;
	double hi = fast_two_sum(x, t0, &e1);
	/* + y cos x */
	return hi + (e1 + t0e + rest + y * (1 - 0.5 * z + z * z * (1.0 / 24)));
}

/* cos(x + y), |x| <= pi/4, |y| tiny */
hidden double __cos_k(double x, double y)
{
	double ze;
	double z = two_prod(x, x, &ze);
	double hz = 0.5 * z;
	double w = 1.0 - hz;
	double lo = ((1.0 - w) - hz) - 0.5 * ze;
	/* + x^4/24 in double-double, then x^6 P(z) */
	double z2e;
	double z2 = two_prod(z, z, &z2e);
	z2e += 2 * z * ze;
	double te;
	double t = two_prod(z2, TWENTYFOURTH_HI, &te);
	te += z2e * TWENTYFOURTH_HI + z2 * TWENTYFOURTH_LO;
	double rest = z2 * z * (COS_C0 + z * (COS_C1 + z * (COS_C2 + z * (COS_C3 + z * (COS_C4 + z * COS_C5)))));
	double e;
	double hi = fast_two_sum(w, t, &e);
	/* - y sin x */
	return hi + (lo + e + te + rest - y * (x - x * z * (1.0 / 6)));
}

/* tan(x + y) for |x| <= pi/4 as hi + *lo */
static double tan_dd(double x, double y, double *lo)
{
	int big = fabs(x) > 0.6744;
	int sgn = x < 0;
	if (big) {
		/* tan(pi/4 - t) = 1 - 2 tan t / (1 + tan t) */
		if (sgn) {
			x = -x;
			y = -y;
		}
		double e;
		double t = two_sum(PIO4_HI - x, PIO4_LO - y, &e);
		x = t;
		y = e;
	}
	double ze;
	double z = two_prod(x, x, &ze);
	double x3e;
	double x3 = two_prod(z, x, &x3e);
	x3e += ze * x;
	double t0e;
	double t0 = two_prod(x3, THIRD_HI, &t0e);
	t0e += x3e * THIRD_HI + x3 * THIRD_LO;
	/* x^5 (C0 + z Q(z)): x^5 C0 is large enough here to need x^5 exact */
	double x5e;
	double x5 = two_prod(x3, z, &x5e);
	x5e += x3e * z + x3 * ze;
	double c0e;
	double c0 = two_prod(x5, TAN_C0, &c0e);
	double qz = TAN_C1 + z * (TAN_C2 + z * (TAN_C3 + z * (TAN_C4 + z * (TAN_C5 + z * (TAN_C6 +
	           z * (TAN_C7 + z * (TAN_C8 + z * (TAN_C9 + z * (TAN_C10 + z * (TAN_C11 + z * TAN_C12))))))))));
	double rest = c0 + (c0e + x5e * TAN_C0 + x5 * z * qz);
	double e1;
	double hi = fast_two_sum(x, t0, &e1);
	double l = e1 + t0e + rest;
	hi = fast_two_sum(hi, l, &l);
	l += y * (1 + hi * hi); /* + y sec^2 x */
	if (!big) {
		*lo = l;
		return hi;
	}
	/* u = tan t = hi + l; result 1 - 2u/(1+u). q = 2u/d, d = 1 + u */
	double de;
	double d = two_sum(1.0, hi, &de);
	de += l;
	double q = 2 * hi / d;
	double qde;
	double qd = two_prod(q, d, &qde);
	double qe = (((2 * hi - qd) - qde) + 2 * l - q * de) / d;
	double el;
	double r = fast_two_sum(1.0, -q, &el);
	el -= qe;
	if (sgn) {
		r = -r;
		el = -el;
	}
	*lo = el;
	return r;
}

hidden double __tan_k(double x, double y, int odd)
{
	double lo;
	double hi = tan_dd(x, y, &lo);
	if (!odd)
		return hi + lo;
	/* -1/(hi + lo) = inv + inv (err + inv lo), err = 1 + inv hi */
	double inv = -1.0 / hi;
	double pe;
	double p = two_prod(inv, hi, &pe);
	double err = (1.0 + p) + pe;
	return inv + inv * (err + inv * lo);
}

double sin(double x)
{
	uint64_t ix = asu64(x) & 0x7fffffffffffffffULL;
	if (ix < 0x3e40000000000000ULL) /* |x| < 2^-27 */
		return x == 0 ? x : x - x * 0x1p-60;
	if (ix >= 0x7ff0000000000000ULL)
		return x != x ? x + x : math_invalid(x);
	double y[2];
	int n = __rem_pio2(x, y);
	switch (n & 3) {
	case 0: return __sin_k(y[0], y[1]);
	case 1: return __cos_k(y[0], y[1]);
	case 2: return -__sin_k(y[0], y[1]);
	default: return -__cos_k(y[0], y[1]);
	}
}

double cos(double x)
{
	uint64_t ix = asu64(x) & 0x7fffffffffffffffULL;
	if (ix < 0x3e40000000000000ULL)
		return 1.0 - 0x1p-60;
	if (ix >= 0x7ff0000000000000ULL)
		return x != x ? x + x : math_invalid(x);
	double y[2];
	int n = __rem_pio2(x, y);
	switch (n & 3) {
	case 0: return __cos_k(y[0], y[1]);
	case 1: return -__sin_k(y[0], y[1]);
	case 2: return -__cos_k(y[0], y[1]);
	default: return __sin_k(y[0], y[1]);
	}
}

double tan(double x)
{
	uint64_t ix = asu64(x) & 0x7fffffffffffffffULL;
	if (ix < 0x3e40000000000000ULL)
		return x == 0 ? x : x + x * 0x1p-60;
	if (ix >= 0x7ff0000000000000ULL)
		return x != x ? x + x : math_invalid(x);
	double y[2];
	int n = __rem_pio2(x, y);
	return __tan_k(y[0], y[1], n & 1);
}
