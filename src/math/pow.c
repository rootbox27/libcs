/* pow: exp(y log x) with log x and the product in double-double.
 * Special cases follow C Annex F. */
#include "libm.h"

/* 0: not an integer, 1: odd integer, 2: even integer */
static int int_kind(double y)
{
	uint64_t iy = asu64(y);
	int e = (int)(iy >> 52 & 0x7ff) - 0x3ff;
	if (e < 0)
		return 0;
	if (e > 52)
		return 2;
	uint64_t frac = (1ULL << (52 - e)) - 1;
	if (iy & frac)
		return 0;
	return (iy >> (52 - e)) & 1 ? 1 : 2;
}

double pow(double x, double y)
{
	uint64_t ix = asu64(x);
	if (y == 0 || x == 1)
		return 1.0;
	if (x != x || y != y)
		return x + y;
	double ax = fabs(x);
	if (isinf(y)) {
		if (ax == 1)
			return 1.0;
		return (ax < 1) == (y < 0) ? INFINITY : 0.0;
	}
	int yk = int_kind(y);
	int neg = (ix >> 63) && yk == 1;
	if (x == 0) {
		if (y < 0)
			return math_divzero(neg);
		return neg ? -0.0 : 0.0;
	}
	if (isinf(x)) {
		if (y < 0)
			return neg ? -0.0 : 0.0;
		return neg ? -INFINITY : INFINITY;
	}
	if ((ix >> 63) && !yk)
		return math_invalid(x); /* negative base, non-integer power */
	if (y == 1)
		return x;
	if (y == 2)
		return x * x;
	if (y == -1)
		return 1 / x;
	if (y == 0.5)
		return sqrt_(ax); /* x is +0 or positive here */

	if (ax == 1)
		return neg ? -1.0 : 1.0; /* x = -1 with an integer y */
	if (fabs(y) > 0x1p60) {
		/* |y log x| is huge: the result over- or underflows (this also
		 * keeps the exact product below from overflowing) */
		if ((ax > 1) == (y > 0))
			return math_oflow(neg);
		return math_uflow(neg);
	}
	double llo;
	double lhi = __log_dd(ax, &llo);
	/* y * (lhi + llo) */
	double pe;
	double p = two_prod(y, lhi, &pe);
	double plo = pe + y * llo;
	if (!(fabs(p) < 0x1p10)) {
		/* certainly overflows or underflows (|y log x| >= 1024) */
		if (p > 0)
			return math_oflow(neg);
		return math_uflow(neg);
	}
	double e;
	double hi = fast_two_sum(p, plo, &e);
	int ok;
	double r = __exp_dd(hi, e, &ok);
	return neg ? -r : r;
}
