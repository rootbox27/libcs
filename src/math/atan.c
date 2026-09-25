/* atan, atan2, asin, acos.
 *
 * atan reduces x >= 0 to |t| <= 7/16 with atan x = atan c + atan t for
 * c in {0, 1/2, 1, 3/2, inf}, t = (x - c)/(1 + c x) as a double-double.
 * asin uses its series for |x| <= 1/2 and asin x = pi/2 - 2 asin(s),
 * s = sqrt((1 - x)/2), above. Leading terms are summed in double-double. */
#include "libm.h"
#include "tables.h"

/* atan(t + tl) - atan c for |t| <= 7/16, added to (ahi + alo); hi + *lo */
static double atan_poly(double t, double tl, double ahi, double alo, double *lo)
{
	double ze;
	double z = two_prod(t, t, &ze);
	double t3e;
	double t3 = two_prod(z, t, &t3e);
	t3e += ze * t;
	double c3e;
	double c3 = two_prod(t3, -THIRD_HI, &c3e);
	c3e -= t3e * THIRD_HI + t3 * THIRD_LO;
	double p = t3 * z * (ATAN_C0 + z * (ATAN_C1 + z * (ATAN_C2 + z * (ATAN_C3 + z * (ATAN_C4 + z * (ATAN_C5 +
	           z * (ATAN_C6 + z * (ATAN_C7 + z * (ATAN_C8 + z * (ATAN_C9 + z * (ATAN_C10 + z * (ATAN_C11 + z * ATAN_C12))))))))))));
	double e1, e2;
	double hi = fast_two_sum(ahi, t, &e1); /* ahi is 0 or larger than |t| */
	hi = two_sum(hi, c3, &e2);
	*lo = e1 + e2 + alo + c3e + p + tl * (1 - z); /* d/dt atan = 1/(1+t^2) */
	return hi;
}

/* atan(x + xl) for x >= 0 finite, |xl| <= ulp(x); hi + *lo */
static double atan_core(double x, double xl, double *lo)
{
	double nh, nl, dh, dl, ahi, alo;
	if (x < 0x1.cp-2) /* 7/16 */
		return atan_poly(x, xl, 0, 0, lo);
	if (x < 0x1.6p-1) { /* 11/16: c = 1/2 */
		nh = x - 0.5;
		nl = xl;
		dh = two_sum(1.0, 0.5 * x, &dl);
		dl += 0.5 * xl;
		ahi = ATAN_HI0;
		alo = ATAN_LO0;
	} else if (x < 0x1.3p0) { /* 19/16: c = 1 */
		nh = x - 1.0;
		nl = xl;
		dh = two_sum(x, 1.0, &dl);
		dl += xl;
		ahi = ATAN_HI1;
		alo = ATAN_LO1;
	} else if (x < 0x1.38p1) { /* 39/16: c = 3/2 */
		nh = x - 1.5;
		nl = xl;
		double pe;
		double p = two_prod(1.5, x, &pe);
		dh = two_sum(1.0, p, &dl);
		dl += pe + 1.5 * xl;
		ahi = ATAN_HI2;
		alo = ATAN_LO2;
	} else { /* c = inf: atan x = pi/2 + atan(-1/x) */
		nh = -1.0;
		nl = 0;
		dh = x;
		dl = xl;
		ahi = ATAN_HI3;
		alo = ATAN_LO3;
	}
	double t = nh / dh;
	double qe;
	double q = two_prod(t, dh, &qe);
	double tl = (((nh - q) - qe) + nl - t * dl) / dh;
	return atan_poly(t, tl, ahi, alo, lo);
}

double atan(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double s = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e40000000000000ULL) /* |x| < 2^-27: atan x = x - x^3/3 */
		return x == 0 ? x : x - x * 0x1p-60;
	if (ax >= 0x4410000000000000ULL) { /* |x| >= 2^66, inf, NaN */
		if (ax > 0x7ff0000000000000ULL)
			return x + x;
		return s * (PIO2_HI + PIO2_LO);
	}
	double lo;
	double hi = atan_core(fabs(x), 0, &lo);
	return s * (hi + lo);
}

double atan2(double y, double x)
{
	if (x != x || y != y)
		return x + y;
	uint64_t ix = asu64(x), iy = asu64(y);
	int xneg = ix >> 63;
	double sy = (iy >> 63) ? -1.0 : 1.0;
	double ax = fabs(x), ay = fabs(y);
	if (ay == 0) {
		/* atan2(+-0, x): +-0 for x > 0 or +0, +-pi for x < 0 or -0 */
		return xneg ? sy * (PI_HI + PI_LO) : y;
	}
	if (ax == 0)
		return sy * (PIO2_HI + PIO2_LO);
	if (isinf(ax)) {
		if (isinf(ay))
			return sy * (xneg ? 0x1.2d97c7f3321d2p+1 : PIO4_HI + PIO4_LO);
		return xneg ? sy * (PI_HI + PI_LO) : sy * 0.0;
	}
	if (isinf(ay))
		return sy * (PIO2_HI + PIO2_LO);

	/* both finite and nonzero: keep the quotient's residual computation
	 * clear of overflow and underflow */
	int ex = (int)(ix >> 52 & 0x7ff), ey = (int)(iy >> 52 & 0x7ff);
	int swap = ay > ax;
	double num = swap ? ax : ay, den = swap ? ay : ax;
	int en = swap ? ex : ey, ed = swap ? ey : ex;
	double hi, lo;
	if (ed - en > 60) {
		/* quotient < 2^-59: atan r = r to well below an ulp */
		if (!swap && !xneg)
			return y / x; /* correctly rounded, even subnormal */
		hi = num / den;
		lo = 0;
	} else {
		if (ed > 0x7fe - 60 || en < 60) {
			/* rescale both by the same power of two */
			double k = ed > 0x7fe - 60 ? 0x1p-600 : 0x1p600;
			num *= k;
			den *= k;
		}
		double r = num / den;
		double qe;
		double q = two_prod(r, den, &qe);
		double rl = ((num - q) - qe) / den;
		hi = atan_core(r, rl, &lo);
	}
	/* result = sy * (base +- (hi + lo)) */
	double e;
	if (swap) {
		/* pi/2 - atan(ax/ay) for x > 0, pi/2 + atan(ax/ay) for x < 0 */
		double h = hi, l = lo;
		if (!xneg) {
			h = -h;
			l = -l;
		}
		hi = fast_two_sum(PIO2_HI, h, &e);
		lo = e + PIO2_LO + l;
	} else if (xneg) {
		hi = fast_two_sum(PI_HI, -hi, &e);
		lo = e + PI_LO - lo;
	}
	return sy * (hi + lo);
}

/* asin(x + xl) for |x| <= 1/2; hi + *lo */
static double asin_core(double x, double xl, double *lo)
{
	double ze;
	double z = two_prod(x, x, &ze);
	double x3e;
	double x3 = two_prod(z, x, &x3e);
	x3e += ze * x;
	double c3e;
	double c3 = two_prod(x3, SIXTH_HI, &c3e);
	c3e += x3e * SIXTH_HI + x3 * SIXTH_LO;
	/* x^5 (C0 + z Q(z)) with x^5 C0 formed exactly */
	double x5e;
	double x5 = two_prod(x3, z, &x5e);
	x5e += x3e * z + x3 * ze;
	double c5e;
	double c5 = two_prod(x5, ASIN_C0, &c5e);
	double q = ASIN_C1 + z * (ASIN_C2 + z * (ASIN_C3 + z * (ASIN_C4 + z * (ASIN_C5 + z * (ASIN_C6 + z * (ASIN_C7 +
	           z * (ASIN_C8 + z * (ASIN_C9 + z * (ASIN_C10 + z * (ASIN_C11 + z * (ASIN_C12 + z * (ASIN_C13 +
	           z * (ASIN_C14 + z * ASIN_C15)))))))))))));
	double p = c5 + (c5e + x5e * ASIN_C0 + x5 * z * q);
	double e;
	double hi = fast_two_sum(x, c3, &e);
	/* d/dx asin = 1/sqrt(1 - z) */
	*lo = e + c3e + p + xl * (1 + z * (0.5 + z * (0.375 + z * 0.3125)));
	return hi;
}

/* 2 asin(sqrt((1 - a)/2)) for 1/2 < a <= 1; hi + *lo */
static double asin_upper(double a, double *lo)
{
	double w = (1.0 - a) * 0.5; /* exact */
	double s = sqrt_(w);
	double pe;
	double p = two_prod(s, s, &pe);
	double sl = ((w - p) - pe) / (2 * s);
	double l;
	double h = asin_core(s, sl, &l);
	*lo = 2 * l;
	return 2 * h;
}

double asin(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double s = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e40000000000000ULL) /* |x| < 2^-27 */
		return x == 0 ? x : x + x * 0x1p-60;
	if (ax >= 0x3ff0000000000000ULL) {
		if (ax == 0x3ff0000000000000ULL)
			return s * (PIO2_HI + PIO2_LO);
		return x != x ? x + x : math_invalid(x);
	}
	double hi, lo, e;
	if (ax <= 0x3fe0000000000000ULL) {
		hi = asin_core(x, 0, &lo);
		return hi + lo;
	}
	double h, l;
	h = asin_upper(fabs(x), &l);
	hi = fast_two_sum(PIO2_HI, -h, &e);
	lo = e + PIO2_LO - l;
	return s * (hi + lo);
}

double acos(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	if (ax >= 0x3ff0000000000000ULL) {
		if (x == 1)
			return 0;
		if (x == -1)
			return PI_HI + PI_LO;
		return x != x ? x + x : math_invalid(x);
	}
	if (ax < 0x3e40000000000000ULL) /* |x| < 2^-27: acos x = pi/2 - x */
		return PIO2_HI + (PIO2_LO - x);
	double hi, lo, e, h, l;
	if (ax <= 0x3fe0000000000000ULL) {
		/* pi/2 - asin x */
		h = asin_core(x, 0, &l);
		hi = fast_two_sum(PIO2_HI, -h, &e);
		lo = e + PIO2_LO - l;
		return hi + lo;
	}
	h = asin_upper(fabs(x), &l);
	if (!(ix >> 63))
		return h + l;
	hi = fast_two_sum(PI_HI, -h, &e);
	lo = e + PI_LO - l;
	return hi + lo;
}
