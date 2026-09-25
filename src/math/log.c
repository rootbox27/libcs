/* log, log2, log10, log1p and the double-double log kernel for pow.
 *
 * Near 1 (|x-1| < 1/16) log1p(r) is a polynomial in r = x - 1 (exact).
 * Elsewhere x = 2^k z with z in [0x1.6p-1, 0x1.6p0), z = c (1 + r) with c
 * from a 128-entry table: log x = k ln2 + log c + log1p(r). The result is
 * formed in double-double, accurate to about 2^-66. */
#include "libm.h"
#include "tables.h"

#define NEAR1_LO 0x3fee000000000000ULL /* 1 - 1/16 */
#define NEAR1_HI 0x3ff1090000000000ULL /* 1 + 0x1.09p-4 */

/* log1p(r) for |r| < 0x1.09p-4, as hi + *lo. The r^2/2 and r^3/3 terms
 * are carried in double-double: pow needs about 2^-70 relative. */
static double log1p_small(double r, double *lo)
{
	double pe;
	double p = two_prod(r, r, &pe);         /* r^2 = p + pe */
	double e1;
	double hi = fast_two_sum(r, -0.5 * p, &e1);
	/* r^3/3 = (a + al) (THIRD_HI + THIRD_LO) */
	double ae;
	double a = two_prod(p, r, &ae);
	double al = ae + pe * r;
	double ce;
	double c = two_prod(a, THIRD_HI, &ce);
	double cl = ce + a * THIRD_LO + al * THIRD_HI;
	double e2;
	hi = two_sum(hi, c, &e2);
	double r4 = p * p;
	double q = LOG1_C0 + r * (LOG1_C1 + r * (LOG1_C2 + r * (LOG1_C3 + r * (LOG1_C4 + r * (LOG1_C5 +
	           r * (LOG1_C6 + r * (LOG1_C7 + r * (LOG1_C8 + r * (LOG1_C9 + r * (LOG1_C10 + r * LOG1_C11))))))))));
	*lo = e1 - 0.5 * pe + e2 + cl + r4 * q;
	return hi;
}

/* log(x) for positive normal or subnormal finite x, as hi + *lo */
static double log_core(double x, double *lo)
{
	uint64_t ix = asu64(x);
	if (ix - NEAR1_LO < NEAR1_HI - NEAR1_LO)
		return log1p_small(x - 1.0, lo); /* x - 1 is exact here */
	int kadj = 0;
	if (ix < 0x0010000000000000ULL) {
		/* subnormal: normalise */
		ix = asu64(x * 0x1p52);
		kadj = -52;
	}
	uint64_t tmp = ix - LOG_OFF;
	int i = (int)((tmp >> 45) % LOG_N);
	int k = (int)((int64_t)tmp >> 52) + kadj;
	double z = asdbl(ix - (tmp & 0xfffULL << 52));
	const double *t = &log_tab[5 * i];
	double invc = t[0], chi = t[1], clo = t[2], lhi = t[3], llo = t[4];
	/* r = z/c - 1 = (z - c) * invc, as a double-double */
	double rl;
	double r = two_prod(z - chi, invc, &rl);
	rl -= clo * invc;
	double kd = k;
	double e1, e2, e3;
	double t1 = two_sum(kd * LOG_LN2HI, lhi, &e1);
	double hi = two_sum(t1, r, &e2);
	double se;
	double s = two_prod(r, r, &se);
	hi = two_sum(hi, -0.5 * s, &e3);
	double poly = s * r * (LOG_C0 + r * (LOG_C1 + r * (LOG_C2 + r * (LOG_C3 + r * (LOG_C4 + r * LOG_C5)))));
	/* rl is not small relative to r (it carries -c_lo/c), so keep the
	 * first-order cross term of log1p(r + rl): rl (1 - r) */
	*lo = e1 + e2 + e3 + (kd * LOG_LN2LO + llo) + (rl - r * rl) - 0.5 * se + poly;
	return hi;
}

hidden double __log_dd(double x, double *lo)
{
	return log_core(x, lo);
}

/* Handles x <= 0, infinities and NaN; returns 1 if it did. */
static int log_special(double x, double *res)
{
	uint64_t ix = asu64(x);
	if (ix - 0x0010000000000000ULL < 0x7ff0000000000000ULL - 0x0010000000000000ULL)
		return 0; /* positive normal */
	if (x == 0)
		*res = math_divzero(1);
	else if (x != x)
		*res = x + x;
	else if (ix >> 63)
		*res = math_invalid(x);
	else if (ix == 0x7ff0000000000000ULL)
		*res = x;
	else
		return 0; /* positive subnormal */
	return 1;
}

double log(double x)
{
	double r, lo;
	if (log_special(x, &r))
		return r;
	double hi = log_core(x, &lo);
	return hi + lo;
}

static double scaled_log(double x, double mhi, double mlo)
{
	double r, lo;
	if (log_special(x, &r))
		return r;
	double hi = log_core(x, &lo);
	/* (hi + lo) * (mhi + mlo) */
	double pe;
	double p = two_prod(hi, mhi, &pe);
	return p + (pe + hi * mlo + lo * mhi);
}

double log2(double x)
{
	uint64_t ix = asu64(x);
	/* exact powers of two give exact integers */
	if (!(ix << 12) && ix - 0x0010000000000000ULL < 0x7ff0000000000000ULL - 0x0010000000000000ULL)
		return (int)(ix >> 52) - 0x3ff;
	return scaled_log(x, INVLN2_HI, INVLN2_LO);
}

double log10(double x)
{
	return scaled_log(x, INVLN10_HI, INVLN10_LO);
}

double log1p(double x)
{
	uint64_t ix = asu64(x);
	if ((ix & 0x7fffffffffffffffULL) < 0x3ca0000000000000ULL)
		return x; /* |x| < 2^-53 */
	if (x <= -1.0) {
		if (x == -1.0)
			return math_divzero(1);
		return math_invalid(x);
	}
	if (!isfinite(x))
		return x + x;
	if (fabs(x) < 0x1p-4) {
		double lo;
		double hi = log1p_small(x, &lo);
		return hi + lo;
	}
	/* 1 + x = u + c exactly; log1p(x) = log(u) + c/u */
	double c;
	double u = two_sum(1.0, x, &c);
	double lo;
	double hi = log_core(u, &lo);
	return hi + (lo + c / u);
}
