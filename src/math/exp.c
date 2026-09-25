/* exp, exp2, expm1 and the double-double exp kernel used by pow.
 *
 * x = (k/N) ln2 + r with |r| <= ln2/(2N), N = 128; exp(x) = 2^(k/N) e^r
 * with 2^(j/N) from a hi+lo table and e^r - 1 from a minimax polynomial.
 * Results in the subnormal range are rounded once. */
#include "libm.h"
#include "tables.h"

#define N EXP_N

/* 2^e * y for y = hi + lo (y near [1, 2)), with a single rounding even
 * when the result is subnormal. */
static double scale(double hi, double lo, int e)
{
	if (e > 1020) {
		double y = hi + lo;
		return y * 0x1p1020 * asdbl((uint64_t)(0x3ff + e - 1020) << 52);
	}
	if (e >= -1021)
		return (hi + lo) * asdbl((uint64_t)(0x3ff + e) << 52);
	/* Work in units where the subnormal ulp is 2^-52, i.e. scaled by
	 * 2^1022: adding 1 moves the rounding point to exactly that ulp. */
	if (e < -1022 - 60)
		return math_uflow(0);
	double s = asdbl((uint64_t)(0x3ff + e + 1022) << 52);
	double a = hi * s, b = lo * s; /* exact: s is a power of two >= 2^-60 */
	double err;
	double y = two_sum(a, b, &err);
	if (y < 1.0) {
		double t = 1.0 + y;
		double l = ((1.0 - t) + y) + err;
		y = (t + l) - 1.0;
		if (y == 0)
			return math_uflow(0);
		force_eval(math_uflow(0) + 0x1p-1022); /* underflow flag */
	} else {
		y += err;
	}
	return y * 0x1p-1022;
}

/* exp(k/N * ln2 + r + rlo) with k = kd, returned as 2^e (hi + lo) form. */
static double exp_core(double r, double rlo, long k)
{
	int j = (int)(k & (N - 1));
	int e = (int)(k >> 7);
	double thi = exp_tab[2 * j], tlo = exp_tab[2 * j + 1];
	double r2 = r * r;
	/* p = e^(r+rlo) - 1 = (e^r - 1) + rlo e^r; rlo can be ~2^-43 (from
	 * pow), so its product with r matters */
	double p = r + (rlo + rlo * r + r2 * (EXP_C0 + r * EXP_C1 + r2 * (EXP_C2 + r * EXP_C3)));
	double lo;
	double hi = fast_two_sum(thi, thi * p + tlo, &lo);
	return scale(hi, lo, e);
}

static long round_to_long(double z)
{
	/* adding 1.5*2^52 rounds to an integer in the low bits */
	double kd = opaque(z + 0x1.8p52) - 0x1.8p52;
	return (long)kd;
}

double exp(double x)
{
	uint64_t ix = asu64(x);
	unsigned top = ix >> 52 & 0x7ff;
	if (top - 0x3c9 >= 0x408 - 0x3c9) {
		if (top < 0x3c9)
			return 1.0 + x; /* |x| < 2^-54 */
		if (top == 0x7ff) {
			if (ix == 0xfff0000000000000ULL)
				return 0.0;
			return 1.0 + x; /* +inf or NaN */
		}
		if (x > 0x1.62e42fefa39efp+9)
			return math_oflow(0);
		if (x < -0x1.74910d52d3052p+9)
			return math_uflow(0);
	}
	long k = round_to_long(EXP_INVLN2_N * x);
	double kd = (double)k;
	double r = (x - kd * EXP_LN2HI_N) - kd * EXP_LN2LO_N;
	return exp_core(r, 0, k);
}

double exp2(double x)
{
	uint64_t ix = asu64(x);
	unsigned top = ix >> 52 & 0x7ff;
	if (top - 0x3c9 >= 0x408 - 0x3c9) {
		if (top < 0x3c9)
			return 1.0 + x;
		if (top == 0x7ff) {
			if (ix == 0xfff0000000000000ULL)
				return 0.0;
			return 1.0 + x;
		}
		if (x >= 1024)
			return math_oflow(0);
		if (x <= -1075)
			return math_uflow(0);
	}
	long k = round_to_long(x * N);
	double t = x - (double)k / N; /* exact */
	/* r + rlo = t * ln2 */
	double rlo;
	double r = two_prod(t, 0x1.62e42fefa39efp-1, &rlo);
	rlo += t * 0x1.abc9e3b39803fp-56;
	return exp_core(r, rlo, k);
}

/* exp(hi + lo), |lo| <= 2^-50 |hi| roughly; for pow. *ok is 0 if the
 * result overflowed or underflowed completely. */
hidden double __exp_dd(double hi, double lo, int *ok)
{
	*ok = 1;
	if (!(hi < 0x1.62e42fefa39efp+9 + 1)) {
		if (hi != hi)
			return hi;
		*ok = 0;
		return math_oflow(0);
	}
	if (hi < -0x1.74910d52d3052p+9 - 1) {
		*ok = 0;
		return math_uflow(0);
	}
	if (hi > 0x1.62e42fefa39efp+9 && hi + lo > 0x1.62e42fefa39efp+9) {
		/* possibly representable right at the edge: let the scaling decide */
	}
	long k = round_to_long(EXP_INVLN2_N * hi);
	double kd = (double)k;
	double r = (hi - kd * EXP_LN2HI_N) - kd * EXP_LN2LO_N;
	return exp_core(r, lo, k);
}

double expm1(double x)
{
	uint64_t ix = asu64(x);
	unsigned top = ix >> 52 & 0x7ff;
	double ax = fabs(x);
	if (top < 0x3c9)
		return x; /* |x| < 2^-54: x + x^2/2 rounds to x */
	if (top == 0x7ff) {
		if (ix == 0xfff0000000000000ULL)
			return -1.0;
		return x + x;
	}
	if (x > 0x1.62e42fefa39efp+9)
		return math_oflow(0);
	if (x < -38)
		return -1.0 + 0x1p-100; /* -1, inexact */
	if (ax <= 0x1.62e42fefa39efp-2) {
		/* |x| <= ln2/2: x + x^2/2 + x^3 P(x) with care in the sum */
		double e2;
		double s = two_prod(x, x, &e2);
		double h = 0.5 * s, he = 0.5 * e2;
		double x3 = s * x;
		double p = x3 * (EXPM1_C0 + x * (EXPM1_C1 + x * (EXPM1_C2 + x * (EXPM1_C3 + x * (EXPM1_C4 +
		           x * (EXPM1_C5 + x * (EXPM1_C6 + x * (EXPM1_C7 + x * (EXPM1_C8 + x * EXPM1_C9)))))))));
		double lo;
		double hi = fast_two_sum(x, h, &lo);
		return hi + (lo + (he + p));
	}
	long k = round_to_long(EXP_INVLN2_N * x);
	double kd = (double)k;
	double r = (x - kd * EXP_LN2HI_N) - kd * EXP_LN2LO_N;
	int j = (int)(k & (N - 1));
	int e = (int)(k >> 7);
	double thi = exp_tab[2 * j], tlo = exp_tab[2 * j + 1];
	double r2 = r * r;
	double p = r + r2 * (EXP_C0 + r * EXP_C1 + r2 * (EXP_C2 + r * EXP_C3));
	double ylo;
	double yhi = fast_two_sum(thi, thi * p + tlo, &ylo);
	if (e > 1000)
		return scale(yhi, ylo, e); /* -1 is negligible */
	/* 2^e y - 1 */
	double s = asdbl((uint64_t)(0x3ff + e) << 52);
	double sh = yhi * s, sl = ylo * s;
	double ue;
	double u = two_sum(sh, -1.0, &ue);
	return u + (ue + sl);
}

/* exp(x + xl) = 2^*e (hi + *lo) with hi + lo in [0.7, 1.5] and about
 * 2^-63 relative error, for |x| <= 746 (for the hyperbolic functions). */
hidden double __exp_split(double x, double xl, double *lo, int *e)
{
	long k = round_to_long(EXP_INVLN2_N * x);
	double kd = (double)k;
	double r1 = x - kd * EXP_LN2HI_N; /* exact */
	double te;
	double t = two_prod(kd, EXP_LN2LO_N, &te);
	double rl;
	double r = two_sum(r1, -t, &rl);
	rl += xl - te;
	int j = (int)(k & (N - 1));
	*e = (int)(k >> 7);
	double thi = exp_tab[2 * j], tlo = exp_tab[2 * j + 1];
	double r2 = r * r;
	/* e^(r + rl) - 1 = r + small */
	double small = rl + rl * r + r2 * (EXP_C0 + r * EXP_C1 + r2 * (EXP_C2 + r * EXP_C3));
	double pe;
	double p = two_prod(thi, r, &pe);
	double e1;
	double hi = fast_two_sum(thi, p, &e1);
	double l = e1 + pe + tlo + thi * small + tlo * (r + small);
	return fast_two_sum(hi, l, lo);
}
