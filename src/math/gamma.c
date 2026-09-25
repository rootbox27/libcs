/* lgamma, tgamma (and lgamma_r, signgam).
 *
 * Positive arguments are shifted into a base interval with exact
 * double-double products of the shift factors: lgamma(2 + t) =
 * t (C0 + t Q(t)) keeps full relative accuracy at the zeros x = 1, 2, and
 * gamma(2.5 + t) = G0 + G1 t + t^2 R(t). From 12 up Stirling's series is
 * summed in double-double. Negative arguments use the recursion upwards
 * (x > -20) or the reflection formula with an accurate sin(pi x). */
#include "libm.h"
#include "tables.h"

int signgam;

/* (ah + al)(bh + bl) */
static double mul_dd(double ah, double al, double bh, double bl, double *lo)
{
	double pe;
	double p = two_prod(ah, bh, &pe);
	return fast_two_sum(p, pe + ah * bl + al * bh, lo);
}

/* p(t) and p'(t) for coefficients c[0..n-1] (lowest first) */
static double horner_d(const double *c, int n, double t, double *dp)
{
	double p = c[n - 1], d = 0;
	for (int k = n - 2; k >= 0; k--) {
		d = p + t * d;
		p = c[k] + t * p;
	}
	*dp = d;
	return p;
}

static const double lgq[] = { LGAM_Q2, LGAM_Q3, LGAM_Q4, LGAM_Q5, LGAM_Q6, LGAM_Q7, LGAM_Q8, LGAM_Q9, LGAM_Q10,
	LGAM_Q11, LGAM_Q12, LGAM_Q13, LGAM_Q14, LGAM_Q15, LGAM_Q16, LGAM_Q17, LGAM_Q18, LGAM_Q19, LGAM_Q20 };

/* lgamma(2 + t + tl) for |t| <= 0.5, tl tiny, as hi + *lo:
 * t C0 + t^2 Q0 + t^3 Q1 in double-double, then t^4 q(t) */
static double lg_poly(double t, double tl, double *lo)
{
	double dq;
	double q = horner_d(lgq, sizeof lgq / sizeof lgq[0], t, &dq);
	double ae;
	double a = two_prod(t, LGAM_C0_HI, &ae);
	ae += t * LGAM_C0_LO;
	double tte;
	double tt = two_prod(t, t, &tte);
	double be;
	double b = two_prod(tt, LGAM_Q0, &be);
	be += tt * LGAM_Q0_LO + tte * LGAM_Q0;
	double t3e;
	double t3 = two_prod(tt, t, &t3e);
	t3e += tte * t;
	double ce;
	double c = two_prod(t3, LGAM_Q1, &ce);
	ce += t3 * LGAM_Q1_LO + t3e * LGAM_Q1;
	double e1, e2;
	double hi = two_sum(a, b, &e1);
	hi = two_sum(hi, c, &e2);
	/* the derivative is digamma(2 + t) */
	double dig = LGAM_C0_HI + t * (2 * LGAM_Q0 + t * (3 * LGAM_Q1 + t * (4 * q + t * dq)));
	double l = e1 + e2 + ae + be + ce + t3 * t * q + tl * dig;
	return fast_two_sum(hi, l, lo);
}

static const double tgr[] = { TGAM_R1, TGAM_R2, TGAM_R3, TGAM_R4, TGAM_R5, TGAM_R6, TGAM_R7, TGAM_R8, TGAM_R9,
	TGAM_R10, TGAM_R11, TGAM_R12, TGAM_R13, TGAM_R14, TGAM_R15, TGAM_R16, TGAM_R17, TGAM_R18 };

/* gamma(2.5 + t + tl), |t| <= 0.5, as hi + *lo */
static double g_poly(double t, double tl, double *lo)
{
	double dr;
	double r = horner_d(tgr, sizeof tgr / sizeof tgr[0], t, &dr);
	double ae;
	double a = two_prod(t, TGAM_G1_HI, &ae);
	ae += t * TGAM_G1_LO;
	double tte;
	double tt = two_prod(t, t, &tte);
	double be;
	double b = two_prod(tt, TGAM_R0, &be);
	be += tte * TGAM_R0;
	double e1, e2;
	double hi = two_sum(TGAM_G0_HI, a, &e1);
	hi = two_sum(hi, b, &e2);
	double deriv = TGAM_G1_HI + t * (2 * TGAM_R0 + t * (3 * r + t * dr));
	double l = TGAM_G0_LO + e1 + e2 + ae + be + tt * t * r + tl * deriv;
	return fast_two_sum(hi, l, lo);
}

/* log(h + l) */
static double log_dd(double h, double l, double *lo)
{
	double ll;
	double lh = __log_dd(h, &ll);
	return fast_two_sum(lh, ll + l / h, lo);
}

/* lgamma(xh + xl) for xh >= 12, as hi + *lo */
static double stirling(double xh, double xl, double *lo)
{
	double ll;
	double lx = log_dd(xh, xl, &ll);
	if (xh > 0x1p900) {
		/* x (log x - 1): the other terms are below an ulp */
		*lo = 0;
		return xh * (lx - 1.0);
	}
	double al;
	double ah = fast_two_sum(xh, -0.5, &al);
	al += xl;
	double pl;
	double p = mul_dd(ah, al, lx, ll, &pl);
	double e1, e2;
	double s = two_sum(p, -xh, &e1);
	s = two_sum(s, HLOG2PI_HI, &e2);
	double u = 1 / xh, v = u * u;
	double corr = u * (STIR_T0 + v * (STIR_T1 + v * (STIR_T2 + v * (STIR_T3 + v * (STIR_T4 + v * STIR_T5)))));
	return fast_two_sum(s, pl + e1 + e2 - xl + HLOG2PI_LO + corr, lo);
}

/* |sin(pi x)| for non-integer x, as hi + *lo; *neg set if sin(pi x) < 0 */
static double sinpi_abs(double x, double *lo, int *neg)
{
	double n = round(x);
	double r = x - n; /* exact, |r| <= 1/2 */
	long ni = (long)fmod(n, 2.0);
	*neg = (ni != 0) != (r < 0);
	r = fabs(r);
	double ae;
	if (r <= 0.25) {
		double a = two_prod(r, PI_HI, &ae);
		ae += r * PI_LO;
		return __sin_kdd(a, ae, lo);
	}
	double b = 0.5 - r; /* exact */
	double a = two_prod(b, PI_HI, &ae);
	ae += b * PI_LO;
	return __cos_kdd(a, ae, lo);
}

static int is_int(double x) { return x == trunc(x); }

double lgamma_r(double x, int *sg)
{
	*sg = 1;
	if (x != x)
		return x + x;
	if (isinf(x))
		return x * x;
	double lo;
	if (x <= 0 && is_int(x))
		return math_divzero(0);
	if (x > 0) {
		if (x >= 12) {
			double hi = stirling(x, 0, &lo);
			return hi + lo;
		}
		if (x >= 2.5) {
			/* lgamma(x - n) + log((x-1)...(x-n)), x - n in [1.5, 2.5) */
			int n = (int)(x - 1.5);
			double pl = 0, ph = x - 1;
			for (int k = 2; k <= n; k++)
				ph = mul_dd(ph, pl, x - k, 0, &pl);
			double gl;
			double gh = lg_poly((x - n) - 2, 0, &gl);
			double ll;
			double lh = log_dd(ph, pl, &ll);
			double e;
			double s = two_sum(gh, lh, &e);
			return s + (e + gl + ll);
		}
		if (x >= 1.5) {
			double hi = lg_poly(x - 2, 0, &lo);
			return hi + lo;
		}
		/* lgamma(x) = lgamma(x + 1) - log x (- log(x + 1) below 1/2) */
		double gl, gh, ll, lh, e;
		if (x >= 0.5) {
			gh = lg_poly(x - 1, 0, &gl);
		} else {
			gh = lg_poly(x, 0, &gl);
			double ml;
			double mh = __log1p_dd(x, &ml);
			gh = two_sum(gh, -mh, &e);
			gl += e - ml;
		}
		lh = __log_dd(x, &ll);
		double s = two_sum(gh, -lh, &e);
		return s + (e + gl - ll);
	}
	/* negative, non-integer */
	if (x > -20) {
		/* lgamma(x + n) - log|x (x+1) ... (x+n-1)|, x + n in (1.5, 2.5] */
		int n = (int)(2.5 - x);
		double yl;
		double yh = two_sum(x, (double)n, &yl);
		double gl;
		double gh = lg_poly(yh - 2, yl, &gl);
		double dl = 0, dh = 1;
		int negs = 0;
		for (int k = 0; k < n; k++) {
			double fl;
			double fh = two_sum(x, (double)k, &fl);
			if (fh < 0) {
				negs++;
				fh = -fh;
				fl = -fl;
			}
			dh = mul_dd(dh, dl, fh, fl, &dl);
		}
		*sg = negs & 1 ? -1 : 1;
		double ll;
		double lh = log_dd(dh, dl, &ll);
		double e;
		double s = two_sum(gh, -lh, &e);
		return s + (e + gl - ll);
	}
	if (x <= -0x1p52)
		return math_divzero(0);
	/* log(pi) - log|sin(pi x)| - lgamma(1 - x) */
	int neg;
	double sl;
	double sh = sinpi_abs(x, &sl, &neg);
	*sg = neg ? -1 : 1;
	double yl;
	double yh = two_sum(1.0, -x, &yl);
	double gl;
	double gh = stirling(yh, yl, &gl);
	double ll;
	double lh = log_dd(sh, sl, &ll);
	double e1, e2;
	double s = two_sum(LOGPI_HI, -lh, &e1);
	s = two_sum(s, -gh, &e2);
	return s + (e1 + e2 + LOGPI_LO - ll - gl);
}

double lgamma(double x) { return lgamma_r(x, &signgam); }

double tgamma(double x)
{
	if (x != x)
		return x + x;
	if (isinf(x))
		return x > 0 ? x : math_invalid(x);
	if (x == 0)
		return 1 / x;
	if (x < 0 && is_int(x))
		return math_invalid(x);
	if (fabs(x) < 0x1p-54)
		return 1 / x;
	if (x > 171.62437695630272)
		return math_oflow(0);
	double lo, hi;
	if (x >= 12) {
		double al;
		double ah = stirling(x, 0, &al);
		int e;
		double h = __exp_split(ah, al, &lo, &e);
		return __scale_dd(h, lo, e);
	}
	if (x >= 2) {
		/* gamma(x - n) (x-1)...(x-n), x - n in [2, 3) */
		int n = (int)(x - 2);
		double pl = 0, ph = 1;
		for (int k = 1; k <= n; k++)
			ph = mul_dd(ph, pl, x - k, 0, &pl);
		double gl;
		double gh = g_poly((x - n) - 2.5, 0, &gl);
		hi = mul_dd(gh, gl, ph, pl, &lo);
		return hi + lo;
	}
	if (x > -20) {
		/* gamma(x + n) / (x (x+1) ... (x+n-1)), x + n in (2, 3] */
		int n = (int)(3 - x);
		double yl;
		double yh = two_sum(x, (double)n, &yl);
		double gl;
		double gh = g_poly(yh - 2.5, yl, &gl);
		double dl = 0, dh = 1;
		for (int k = 0; k < n; k++) {
			double fl;
			double fh = two_sum(x, (double)k, &fl);
			dh = mul_dd(dh, dl, fh, fl, &dl);
		}
		/* (gh + gl) / (dh + dl) */
		double q = gh / dh;
		double qe;
		double qd = two_prod(q, dh, &qe);
		return q + ((((gh - qd) - qe) + gl - q * dl) / dh);
	}
	/* pi / (sin(pi x) gamma(1 - x)) = +- exp(log pi - log|sin pi x| - lgamma(1 - x)) */
	int neg;
	double sl;
	double sh = sinpi_abs(x, &sl, &neg);
	double yl;
	double yh = two_sum(1.0, -x, &yl);
	double gl;
	double gh = stirling(yh, yl, &gl);
	double ll;
	double lh = log_dd(sh, sl, &ll);
	double e1, e2;
	double s = two_sum(LOGPI_HI, -lh, &e1);
	s = two_sum(s, -gh, &e2);
	double bl = e1 + e2 + LOGPI_LO - ll - gl;
	double bh = fast_two_sum(s, bl, &bl);
	if (bh < -746)
		return math_uflow(neg);
	int e;
	double h = __exp_split(bh, bl, &lo, &e);
	double r = __scale_dd(h, lo, e);
	return neg ? -r : r;
}

float lgammaf_r(float x, int *sg) { return (float)lgamma_r(x, sg); }
float lgammaf(float x) { return (float)lgamma_r(x, &signgam); }
float tgammaf(float x) { return (float)tgamma(x); }
