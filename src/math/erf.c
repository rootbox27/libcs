/* erf, erfc.
 *
 * |x| < 1: erf(x) = x (C + P0 z + P1 z^2 + z^3 R(z)), z = x^2, with the
 * leading terms in double-double. 1 <= x < 27.3: erfc(x) = e^(-x^2) w(x)
 * with w on each of 16 intervals as W0 + W1 t + t^2 R(t) and e^(-x^2)
 * from the double-double exp kernel; erf = 1 - erfc there. */
#include "libm.h"
#include "tables.h"

/* erf(x) for |x| < 1 as hi + *lo */
static double erf_small(double x, double *lo)
{
	double ze;
	double z = two_prod(x, x, &ze);
	double z2e;
	double z2 = two_prod(z, z, &z2e);
	z2e += 2 * z * ze;
	double r = ERF_R0 + z * (ERF_R1 + z * (ERF_R2 + z * (ERF_R3 + z * (ERF_R4 + z * (ERF_R5 + z * (ERF_R6 +
	           z * (ERF_R7 + z * (ERF_R8 + z * (ERF_R9 + z * ERF_R10)))))))));
	/* s = C + P0 z + P1 z^2 + z^3 r */
	double ae;
	double a = two_prod(z, ERF_P0_HI, &ae);
	ae += z * ERF_P0_LO + ze * ERF_P0_HI;
	double be;
	double b = two_prod(z2, ERF_P1_HI, &be);
	be += z2 * ERF_P1_LO + z2e * ERF_P1_HI;
	double e1, e2;
	double s = fast_two_sum(ERF_C_HI, a, &e1);
	s = fast_two_sum(s, b, &e2);
	double sl = ERF_C_LO + e1 + e2 + ae + be + z2 * z * r;
	s = fast_two_sum(s, sl, &sl);
	double pe;
	double p = two_prod(x, s, &pe);
	return fast_two_sum(p, pe + x * sl, lo);
}

/* erfc(x) = 2^*e (hi + *lo) for 1 <= x < 27.5 */
static double erfc_mid(double x, double *lo, int *e)
{
	int i = 0;
	while (i < ERFC_NINT - 1 && x >= erfc_edges[i + 1])
		i++;
	const double *row = &erfc_tab[i * ERFC_ROW];
	double t = x - row[0]; /* exact: x is near the centre */
	int deg = (int)row[5];
	double rr = row[6 + deg];
	for (int k = deg - 1; k >= 0; k--)
		rr = row[6 + k] + t * rr;
	/* w = W0 + W1 t + t^2 R */
	double ae;
	double a = two_prod(t, row[3], &ae);
	ae += t * row[4];
	double e1;
	double wh = two_sum(row[1], a, &e1);
	double wl = row[2] + e1 + ae + t * t * rr;
	wh = fast_two_sum(wh, wl, &wl);
	/* e^(-x^2) */
	double qe;
	double q = two_prod(x, x, &qe);
	double el;
	double eh = __exp_split(-q, -qe, &el, e);
	double pe;
	double p = two_prod(eh, wh, &pe);
	return fast_two_sum(p, pe + eh * wl + el * wh, lo);
}

double erf(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double sgn = (ix >> 63) ? -1.0 : 1.0;
	if (ax >= 0x7ff0000000000000ULL)
		return x != x ? x + x : sgn;
	if (ax < 0x3c90000000000000ULL) { /* |x| < 2^-54: erf x = C x */
		if (ax < 0x0400000000000000ULL) /* products may be inexact: one rounding */
			return x * ERF_C_HI;
		double pe;
		double p = two_prod(x, ERF_C_HI, &pe);
		return p + (pe + x * ERF_C_LO);
	}
	double lo;
	double a = fabs(x);
	if (a < 1) {
		double hi = erf_small(x, &lo);
		return hi + lo;
	}
	if (a >= 6)
		return sgn * (1.0 - 0x1p-60);
	int e;
	double h = erfc_mid(a, &lo, &e);
	double s = asdbl((uint64_t)(0x3ff + e) << 52);
	double ce;
	double c = two_sum(1.0, -h * s, &ce);
	return sgn * (c + (ce - lo * s));
}

double erfc(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	if (ax >= 0x7ff0000000000000ULL) {
		if (x != x)
			return x + x;
		return (ix >> 63) ? 2.0 : 0.0;
	}
	if (ax < 0x3c90000000000000ULL) /* |x| < 2^-54 */
		return 1.0 - x;
	double lo;
	if (ax < 0x3ff0000000000000ULL) {
		double hi = erf_small(x, &lo);
		double ce;
		double c = two_sum(1.0, -hi, &ce);
		return c + (ce - lo);
	}
	if (x >= 27.3)
		return math_uflow(0);
	if (x <= -6)
		return 2.0 - 0x1p-60;
	int e;
	double h = erfc_mid(fabs(x), &lo, &e);
	if (x > 0)
		return __scale_dd(h, lo, e);
	double s = asdbl((uint64_t)(0x3ff + e) << 52);
	double ce;
	double c = fast_two_sum(2.0, -h * s, &ce);
	return c + (ce - lo * s);
}

float erff(float x) { return (float)erf(x); }
float erfcf(float x) { return (float)erfc(x); }
