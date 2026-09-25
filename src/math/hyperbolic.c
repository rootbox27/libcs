/* sinh, cosh, tanh, asinh, acosh, atanh.
 *
 * Small arguments use odd polynomials; elsewhere the functions are formed
 * from exp and log kernels that return double-doubles, so cancellation
 * (sinh near 0.5, acosh near 1, atanh) costs nothing visible. */
#include "libm.h"
#include "tables.h"

/* (hi + lo) 2^e with a single rounding (e may exceed 1023) */
static double scale2(double hi, double lo, int e)
{
	double y = hi + lo;
	if (e > 1023) {
		y *= 0x1p1023;
		e -= 1023;
	}
	return y * asdbl((uint64_t)(0x3ff + e) << 52);
}

/* e^a +- e^-a for a >= 0.5 (sign = +1 or -1), halved */
static double exp_pm(double a, double sign)
{
	int e;
	double l;
	double h = __exp_split(a, 0, &l, &e);
	if (e > 40) /* e^-a is below 2^-80 of e^a */
		return scale2(h, l, e - 1);
	/* 1/(h + l) */
	double inv = 1.0 / h;
	double pe;
	double p = two_prod(inv, h, &pe);
	double invl = inv * (((1.0 - p) - pe) - inv * l);
	double s1 = asdbl((uint64_t)(0x3ff + e) << 52), s2 = asdbl((uint64_t)(0x3ff - e) << 52);
	double ah = h * s1, al = l * s1, bh = sign * inv * s2, bl = sign * invl * s2;
	double err;
	double s = two_sum(ah, bh, &err);
	return 0.5 * (s + (err + al + bl));
}

double sinh(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double sgn = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e50000000000000ULL) /* |x| < 2^-26 */
		return x == 0 ? x : x + x * 0x1p-60;
	if (ax >= 0x7ff0000000000000ULL)
		return x + x;
	double a = fabs(x);
	if (a < 0.5) {
		double ze;
		double z = two_prod(x, x, &ze);
		double x3e;
		double x3 = two_prod(z, x, &x3e);
		x3e += ze * x;
		double c3e;
		double c3 = two_prod(x3, SIXTH_HI, &c3e);
		c3e += x3e * SIXTH_HI + x3 * SIXTH_LO;
		double p = x3 * z * (SINH_C0 + z * (SINH_C1 + z * (SINH_C2 + z * (SINH_C3 + z * (SINH_C4 + z * SINH_C5)))));
		double e;
		double hi = fast_two_sum(x, c3, &e);
		return hi + (e + c3e + p);
	}
	if (a > 711)
		return math_oflow(ix >> 63);
	return sgn * exp_pm(a, -1.0);
}

double cosh(double x)
{
	uint64_t ax = asu64(x) & 0x7fffffffffffffffULL;
	if (ax < 0x3e50000000000000ULL)
		return 1.0 + 0x1p-60;
	if (ax >= 0x7ff0000000000000ULL)
		return x * x;
	double a = fabs(x);
	if (a > 711)
		return math_oflow(0);
	if (a >= 0.5)
		return exp_pm(a, 1.0);
	/* small: e^a + e^-a with both near 1 */
	int e;
	double l;
	double h = __exp_split(a, 0, &l, &e);
	double s = asdbl((uint64_t)(0x3ff + e) << 52);
	h *= s;
	l *= s;
	double inv = 1.0 / h;
	double pe;
	double p = two_prod(inv, h, &pe);
	double invl = inv * (((1.0 - p) - pe) - inv * l);
	double err;
	double sum = two_sum(h, inv, &err);
	return 0.5 * (sum + (err + l + invl));
}

double tanh(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double sgn = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e40000000000000ULL) /* |x| < 2^-27 */
		return x == 0 ? x : x - x * 0x1p-60;
	if (ax >= 0x7ff0000000000000ULL) {
		if (x != x)
			return x + x;
		return sgn;
	}
	double a = fabs(x);
	if (a >= 22)
		return sgn * (1.0 - 0x1p-60);
	if (a < 0.55) {
		double ze;
		double z = two_prod(x, x, &ze);
		double x3e;
		double x3 = two_prod(z, x, &x3e);
		x3e += ze * x;
		double c3e;
		double c3 = two_prod(x3, -THIRD_HI, &c3e);
		c3e -= x3e * THIRD_HI + x3 * THIRD_LO;
		double x5e;
		double x5 = two_prod(x3, z, &x5e);
		x5e += x3e * z + x3 * ze;
		double c5e;
		double c5 = two_prod(x5, TANH_C0, &c5e);
		double q = TANH_C1 + z * (TANH_C2 + z * (TANH_C3 + z * (TANH_C4 + z * (TANH_C5 + z * (TANH_C6 + z * (TANH_C7 +
		           z * (TANH_C8 + z * (TANH_C9 + z * (TANH_C10 + z * (TANH_C11 + z * TANH_C12))))))))));
		double e1, e2;
		double hi = fast_two_sum(x, c3, &e1);
		hi = two_sum(hi, c5, &e2);
		return hi + (e1 + e2 + c3e + c5e + x5e * TANH_C0 + x5 * z * q);
	}
	/* (1 - u)/(1 + u), u = e^-2a */
	int e;
	double l;
	double h = __exp_split(-2 * a, 0, &l, &e);
	double s = asdbl((uint64_t)(0x3ff + e) << 52);
	double uh = h * s, ul = l * s;
	double nl, dl;
	double n = fast_two_sum(1.0, -uh, &nl);
	nl -= ul;
	double d = fast_two_sum(1.0, uh, &dl);
	dl += ul;
	double q = n / d;
	double qe;
	double qd = two_prod(q, d, &qe);
	double ql = (((n - qd) - qe) + nl - q * dl) / d;
	return sgn * (q + ql);
}

/* log(v + vl) for v > 0 normal */
static double log_dd2(double v, double vl)
{
	double l;
	double h = __log_dd(v, &l);
	return h + (l + vl / v);
}

double asinh(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double sgn = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e50000000000000ULL)
		return x == 0 ? x : x - x * 0x1p-60;
	if (ax >= 0x7ff0000000000000ULL)
		return x + x;
	double a = fabs(x);
	if (a <= 0.5) {
		double ze;
		double z = two_prod(x, x, &ze);
		double x3e;
		double x3 = two_prod(z, x, &x3e);
		x3e += ze * x;
		double c3e;
		double c3 = two_prod(x3, -SIXTH_HI, &c3e);
		c3e -= x3e * SIXTH_HI + x3 * SIXTH_LO;
		double p = x3 * z * (ASINH_C0 + z * (ASINH_C1 + z * (ASINH_C2 + z * (ASINH_C3 + z * (ASINH_C4 + z * (ASINH_C5 +
		           z * (ASINH_C6 + z * (ASINH_C7 + z * (ASINH_C8 + z * (ASINH_C9 + z * (ASINH_C10 + z * (ASINH_C11 +
		           z * (ASINH_C12 + z * (ASINH_C13 + z * (ASINH_C14 + z * (ASINH_C15 + z * ASINH_C16))))))))))))))));
		double e;
		double hi = fast_two_sum(x, c3, &e);
		return hi + (e + c3e + p);
	}
	if (a > 0x1p28) {
		/* log(2a) */
		double l;
		double h = __log_dd(a, &l);
		double e;
		double s = two_sum(h, LOG_LN2HI, &e);
		return sgn * (s + (e + l + LOG_LN2LO));
	}
	/* log(a + sqrt(a^2 + 1)) */
	double pe;
	double p = two_prod(a, a, &pe);
	double we;
	double w = two_sum(p, 1.0, &we);
	we += pe;
	double s = sqrt_(w);
	double se;
	double ss = two_prod(s, s, &se);
	double sl = (((w - ss) - se) + we) / (2 * s);
	double ve;
	double v = fast_two_sum(s, a, &ve);
	return sgn * log_dd2(v, ve + sl);
}

double acosh(double x)
{
	uint64_t ix = asu64(x);
	if (!(x >= 1)) /* also NaN */
		return x != x ? x + x : math_invalid(x);
	if (x == 1)
		return 0;
	if (ix >= 0x7ff0000000000000ULL)
		return x;
	if (x > 0x1p28) {
		double l;
		double h = __log_dd(x, &l);
		double e;
		double s = two_sum(h, LOG_LN2HI, &e);
		return s + (e + l + LOG_LN2LO);
	}
	if (x >= 2) {
		double pe;
		double p = two_prod(x, x, &pe);
		double we;
		double w = fast_two_sum(p, -1.0, &we);
		we += pe;
		double s = sqrt_(w);
		double se;
		double ss = two_prod(s, s, &se);
		double sl = (((w - ss) - se) + we) / (2 * s);
		double ve;
		double v = fast_two_sum(x, s, &ve);
		return log_dd2(v, ve + sl);
	}
	/* x = 1 + t: log1p(t + sqrt(t (2 + t))) */
	double t = x - 1.0; /* exact */
	double be;
	double b = fast_two_sum(2.0, t, &be);
	double ve;
	double v = two_prod(t, b, &ve);
	ve += t * be;
	double s = sqrt_(v);
	double se;
	double ss = two_prod(s, s, &se);
	double sl = (((v - ss) - se) + ve) / (2 * s);
	double ue;
	double u = two_sum(t, s, &ue);
	ue += sl;
	double l;
	double h = __log1p_dd(u, &l);
	return h + (l + ue / (1 + u));
}

double atanh(double x)
{
	uint64_t ix = asu64(x);
	uint64_t ax = ix & 0x7fffffffffffffffULL;
	double sgn = (ix >> 63) ? -1.0 : 1.0;
	if (ax < 0x3e40000000000000ULL) /* |x| < 2^-27 */
		return x == 0 ? x : x + x * 0x1p-60;
	if (ax >= 0x3ff0000000000000ULL) {
		if (ax == 0x3ff0000000000000ULL)
			return math_divzero(ix >> 63);
		return x != x ? x + x : math_invalid(x);
	}
	/* 1/2 log1p(2a/(1 - a)) */
	double a = fabs(x);
	double dl;
	double d = fast_two_sum(1.0, -a, &dl);
	double n = 2 * a;
	double q = n / d;
	double qe;
	double qd = two_prod(q, d, &qe);
	double ql = (((n - qd) - qe) - q * dl) / d;
	double l;
	double h = __log1p_dd(q, &l);
	return sgn * 0.5 * (h + (l + ql / (1 + q)));
}
