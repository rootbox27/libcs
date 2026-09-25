/* long double (x87 80-bit extended) transcendental functions.
 *
 * exp and log have table-driven kernels in double-long-double arithmetic
 * (tables from scripts/gen-math.py), accurate to about 2^-80, so expl,
 * logl, powl and the functions built on them round correctly in nearly
 * all cases. Trigonometric functions reduce the argument themselves
 * (Cody-Waite, or Payne-Hanek shared with the double code) and use the
 * x87 fsin/fcos/fptan instructions only on [-pi/4, pi/4], where they are
 * accurate; inverse trigonometric functions use fpatan. */
#include "libm.h"
#include "tables.h"

typedef long double ld;

/* ---- double-long-double arithmetic (round to nearest) ---- */

static inline ld two_sum_l(ld a, ld b, ld *e)
{
	ld s = a + b, bb = s - a;
	*e = (a - (s - bb)) + (b - bb);
	return s;
}

static inline ld fast_two_sum_l(ld a, ld b, ld *e)
{
	ld s = a + b;
	*e = b - (s - a);
	return s;
}

static inline ld two_prod_l(ld a, ld b, ld *e)
{
	const ld c = 0x1p32L + 1;
	ld p = a * b;
	ld ca = c * a, ah = ca - (ca - a), al = a - ah;
	ld cb = c * b, bh = cb - (cb - b), bl = b - bh;
	*e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
	return p;
}

static inline int ld_exp(ld x)
{
	union ldbits u = { x };
	return u.i.se & 0x7fff;
}

/* 2^n for -16382 <= n <= 16383 */
static inline ld pow2l(int n)
{
	union ldbits u;
	u.i.m = 1ULL << 63;
	u.i.se = (uint16_t)(0x3fff + n);
	return u.f;
}

static inline ld oflowl(int neg) { return math_oflow(neg); }
static inline ld uflowl(int neg) { return math_uflow(neg); }
static inline ld invalidl(ld x) { x = x - x; return x / x; }

/* (hi + lo) 2^e rounded once, also into the subnormal range */
static ld scale_l(ld hi, ld lo, int e)
{
	int eh = ld_exp(hi) - 0x3fff;
	if (eh + e >= -16382) {
		ld y = hi + lo;
		if (e > 16383)
			return y * pow2l(16383) * pow2l(e - 16383);
		if (e < -16382)
			return y * pow2l(-16382) * pow2l(e + 16382);
		return y * pow2l(e);
	}
	if (eh + e < -16382 - 70)
		return uflowl(hi < 0);
	/* in units where the subnormal ulp is 2^-63, adding 1 rounds there */
	ld s = pow2l(e + 16382);
	ld a = hi * s, b = lo * s;
	ld one = hi < 0 ? -1.0L : 1.0L;
	ld te;
	ld t = two_sum_l(one, a, &te);
	ld r = t + (te + b);
	if (r - t != te + b)
		force_eval(math_uflow(0));
	ld v = (r - one) * pow2l(-16382);
	return v == 0 ? one * 0.0L : v;
}

/* ---- exp kernel: e^(wh + wl) = 2^*e (hi + *lo), |wh| < 11500 ---- */
static ld exp_kernel(ld wh, ld wl, ld *lo, int *e)
{
	ld kd = (wh * LEXP_INVLN2_N + 0x1.8p63L) - 0x1.8p63L;
	long k = (long)kd;
	ld r1 = wh - kd * LEXP_LN2HI_N; /* exact */
	ld te;
	ld t = two_prod_l(kd, LEXP_LN2LO_N, &te);
	ld rl;
	ld r = two_sum_l(r1, -t, &rl);
	rl += wl - te;
	r = fast_two_sum_l(r, rl, &rl);
	int j = (int)(k & (LEXP_N - 1));
	*e = (int)(k >> 7);
	ld th = lexp_tab[2 * j], tl = lexp_tab[2 * j + 1];
	/* e^(r + rl) - 1 = r + q */
	ld r2e;
	ld r2 = two_prod_l(r, r, &r2e);
	ld poly = r * r2 * (LEXP_C0 + r * (LEXP_C1 + r * (LEXP_C2 + r * (LEXP_C3 + r * LEXP_C4))));
	ld q = 0.5L * r2 + (0.5L * r2e + poly + rl * (1 + r));
	ld pe, e1;
	ld p = two_prod_l(th, r, &pe);
	ld hi = fast_two_sum_l(th, p, &e1);
	ld l = e1 + pe + th * q + tl + tl * (r + q);
	return fast_two_sum_l(hi, l, lo);
}

/* ---- log kernel: log x = hi + *lo for positive finite x ---- */
static ld log_kernel(ld x, ld *lo)
{
	union ldbits u = { x };
	int k;
	if ((u.i.se & 0x7fff) == 0) {
		u.f = x * 0x1p64L;
		k = (u.i.se & 0x7fff) - 0x3fff - 64;
	} else {
		k = (u.i.se & 0x7fff) - 0x3fff;
	}
	u.i.se = 0x3fff;
	ld m = u.f; /* [1, 2) */
	if (m >= 1.5L) {
		m *= 0.5L;
		k++;
	}
	/* m in [0.75, 1.5): c = 1 + i/128 nearest; for i = 0, c = 1 exactly */
	int i = (int)((m - 1) * 128 + (m >= 1 ? 0.5L : -0.5L));
	const ld *t = &llog_tab[3 * (i + LLOG_I0)];
	ld pe;
	ld p = two_prod_l(m, t[0], &pe);
	ld rl;
	ld r = fast_two_sum_l(p - 1, pe, &rl); /* r + rl = m invc - 1 exactly */
	ld r2e;
	ld r2 = two_prod_l(r, r, &r2e);
	ld poly = r * r2 * (LLOG_C0 + r * (LLOG_C1 + r * (LLOG_C2 + r * (LLOG_C3 + r * (LLOG_C4 + r * (LLOG_C5 +
	          r * (LLOG_C6 + r * LLOG_C7)))))));
	ld kd = k;
	ld e1, e2, e3;
	ld s = two_sum_l(kd * LLN2_HI, t[1], &e1);
	s = two_sum_l(s, r, &e2);
	s = two_sum_l(s, -0.5L * r2, &e3);
	ld l = e1 + e2 + e3 + (kd * LLN2_LO + t[2]) + rl * (1 - r) - 0.5L * r2e + poly;
	return fast_two_sum_l(s, l, lo);
}

/* log1p(x) as hi + *lo, x > -1 finite */
static ld log1p_kernel(ld x, ld *lo)
{
	ld ul;
	ld u = two_sum_l(1.0L, x, &ul);
	ld l;
	ld h = log_kernel(u, &l);
	*lo = l + ul / u;
	return h;
}

ld expl(ld x)
{
	if (x != x)
		return x + x;
	if (x > 11356.523406294143949491931077970765L)
		return x == INFINITY ? x : oflowl(0);
	if (x < -11400.0L)
		return x == -INFINITY ? 0.0L : uflowl(0);
	if (fabsl(x) < 0x1p-65L)
		return 1.0L + x;
	ld lo;
	int e;
	ld hi = exp_kernel(x, 0, &lo, &e);
	return scale_l(hi, lo, e);
}

ld exp2l(ld x)
{
	if (x != x)
		return x + x;
	if (x >= 16384.0L)
		return x == INFINITY ? x : oflowl(0);
	if (x < -16447.0L)
		return x == -INFINITY ? 0.0L : uflowl(0);
	ld n = rintl(x);
	ld f = x - n; /* exact */
	if (f == 0)
		return scale_l(1.0L, 0, (int)n);
	ld wl;
	ld wh = two_prod_l(f, LLN2_H, &wl);
	wl += f * LLN2_L;
	ld lo;
	int e;
	ld hi = exp_kernel(wh, wl, &lo, &e);
	return scale_l(hi, lo, e + (int)n);
}

/* expm1 for |x| <= 0.3466 as hi + *lo */
static ld expm1_small(ld x, ld *lo)
{
	ld x2e;
	ld x2 = two_prod_l(x, x, &x2e);
	ld p = x * x2 * (LEXPM1_C0 + x * (LEXPM1_C1 + x * (LEXPM1_C2 + x * (LEXPM1_C3 + x * (LEXPM1_C4 + x * (LEXPM1_C5 +
	       x * (LEXPM1_C6 + x * (LEXPM1_C7 + x * (LEXPM1_C8 + x * (LEXPM1_C9 + x * (LEXPM1_C10 + x * (LEXPM1_C11 +
	       x * (LEXPM1_C12 + x * LEXPM1_C13)))))))))))));
	ld e;
	ld hi = fast_two_sum_l(x, 0.5L * x2, &e);
	return fast_two_sum_l(hi, e + 0.5L * x2e + p, lo);
}

ld expm1l(ld x)
{
	if (x != x)
		return x + x;
	if (x > 11356.6L)
		return x == INFINITY ? x : oflowl(0);
	if (x < -45.5L)
		return x == -INFINITY ? -1.0L : -1.0L + 0x1p-100L;
	if (fabsl(x) < 0x1p-65L)
		return x;
	ld lo;
	if (fabsl(x) <= 0.3466L) {
		ld hi = expm1_small(x, &lo);
		return hi + lo;
	}
	int e;
	ld hi = exp_kernel(x, 0, &lo, &e);
	if (e > 70)
		return scale_l(hi, lo, e);
	ld s = pow2l(e);
	ld ue;
	ld u = two_sum_l(hi * s, -1.0L, &ue);
	return u + (ue + lo * s);
}

/* NaN, zero, negative and infinite arguments of log; 1 if handled */
static int log_special(ld x, ld *res)
{
	if (x != x) {
		*res = x + x;
		return 1;
	}
	if (x == 0) {
		*res = math_divzero(1);
		return 1;
	}
	if (x < 0) {
		*res = invalidl(x);
		return 1;
	}
	if (x == INFINITY) {
		*res = x;
		return 1;
	}
	return 0;
}

ld logl(ld x)
{
	ld r, lo;
	if (log_special(x, &r))
		return r;
	ld hi = log_kernel(x, &lo);
	return hi + lo;
}

static ld scaled_logl(ld x, ld mh, ld ml)
{
	ld r, lo;
	if (log_special(x, &r))
		return r;
	ld hi = log_kernel(x, &lo);
	ld pe;
	ld p = two_prod_l(hi, mh, &pe);
	return p + (pe + hi * ml + lo * mh);
}

ld log2l(ld x)
{
	union ldbits u = { x };
	int ex = u.i.se & 0x7fff;
	if (!(u.i.se & 0x8000) && ex != 0 && ex != 0x7fff && u.i.m == 1ULL << 63)
		return ex - 0x3fff; /* exact power of two */
	return scaled_logl(x, LINVLN2_H, LINVLN2_L);
}

ld log10l(ld x)
{
	return scaled_logl(x, LINVLN10_H, LINVLN10_L);
}

ld log1pl(ld x)
{
	if (x != x)
		return x + x;
	if (x <= -1.0L)
		return x == -1.0L ? math_divzero(1) : invalidl(x);
	if (x == INFINITY)
		return x;
	if (fabsl(x) < 0x1p-65L)
		return x;
	ld lo;
	ld hi = log1p_kernel(x, &lo);
	return hi + lo;
}

/* ---- pow ---- */

/* 0: not an integer, 1: odd integer, 2: even integer */
static int int_kind_l(ld y)
{
	if (y != y || fabsl(y) == INFINITY)
		return 0;
	if (fabsl(y) >= 0x1p64L)
		return 2;
	if (truncl(y) != y)
		return 0;
	union ldbits u = { y };
	int e = (u.i.se & 0x7fff) - 0x3fff; /* y = m 2^(e-63) */
	if (e < 0)
		return 0;
	return (u.i.m >> (63 - e)) & 1 ? 1 : 2;
}

ld powl(ld x, ld y)
{
	if (y == 0 || x == 1)
		return 1.0L;
	if (x != x || y != y)
		return x + y;
	ld ax = fabsl(x);
	int yk = int_kind_l(y);
	int xneg = signbit(x) != 0;
	int neg = xneg && yk == 1;
	if (fabsl(y) == INFINITY) {
		if (ax == 1)
			return 1.0L;
		return (ax < 1) == (y < 0) ? INFINITY : 0.0L;
	}
	if (x == 0) {
		if (y < 0)
			return math_divzero(neg);
		return neg ? -0.0L : 0.0L;
	}
	if (ax == INFINITY) {
		if (y < 0)
			return neg ? -0.0L : 0.0L;
		return neg ? -INFINITY : INFINITY;
	}
	if (xneg && !yk)
		return invalidl(x);
	if (y == 1)
		return x;
	if (y == 2)
		return x * x;
	if (y == -1)
		return 1 / x;
	if (y == 0.5L)
		return sqrtl(x);
	if (ax == 1)
		return neg ? -1.0L : 1.0L;
	if (fabsl(y) > 0x1p80L) {
		/* |y log x| > 2^14 since |log x| > 2^-65 */
		if ((ax > 1) == (y > 0))
			return oflowl(neg);
		return uflowl(neg);
	}
	ld llo;
	ld lhi = log_kernel(ax, &llo);
	ld pe;
	ld p = two_prod_l(y, lhi, &pe);
	ld plo = pe + y * llo;
	if (!(fabsl(p) < 11500.0L)) {
		if (p > 0)
			return oflowl(neg);
		return uflowl(neg);
	}
	ld e1;
	ld wh = fast_two_sum_l(p, plo, &e1);
	ld lo;
	int e;
	ld hi = exp_kernel(wh, e1, &lo, &e);
	if (neg) {
		hi = -hi;
		lo = -lo;
	}
	return scale_l(hi, lo, e);
}

/* ---- trigonometric ---- */

static int rem_pio2l(ld x, ld *yh, ld *yl)
{
	ld ax = fabsl(x);
	if (ax <= LPIO4_H) {
		*yh = x;
		*yl = 0;
		return 0;
	}
	if (ax < 0x1p30L) {
		long n = (long)(x * LINVPIO2 + (x < 0 ? -0.5L : 0.5L));
		ld fn = n;
		ld a = x - fn * LPIO2_1; /* exact */
		ld e1, e2;
		ld s = two_sum_l(a, -fn * LPIO2_2, &e1);
		s = two_sum_l(s, -fn * LPIO2_3, &e2);
		ld lo = (e1 + e2) - fn * LPIO2_3T;
		*yh = fast_two_sum_l(s, lo, yl);
		return (int)(n & 3);
	}
	union ldbits u = { ax };
	int e = (u.i.se & 0x7fff) - 0x3fff;
	u128 f;
	int neg;
	int n = __rem_pio2_bits(u.i.m, 64, e, &f, &neg);
	ld fh = (ld)(uint64_t)(f >> 64) * 0x1p-64L;
	ld fl = (ld)(uint64_t)f * 0x1p-128L;
	ld pe;
	ld p = two_prod_l(fh, LPIO2_H, &pe);
	ld lo = pe + fh * LPIO2_L + fl * LPIO2_H;
	*yh = fast_two_sum_l(p, lo, yl);
	if (neg != (x < 0)) {
		*yh = -*yh;
		*yl = -*yl;
	}
	if (x < 0)
		n = -n;
	return n & 3;
}

static inline ld x87_sin(ld x) { __asm__("fsin" : "+t"(x)); return x; }
static inline ld x87_cos(ld x) { __asm__("fcos" : "+t"(x)); return x; }
static inline ld x87_tan(ld x) { __asm__("fptan\n\tfstp %%st(0)" : "+t"(x)); return x; }

/* sin and cos of yh + yl, |yh| <= pi/4 */
static ld sin_kl(ld yh, ld yl)
{
	ld z = yh * yh;
	return x87_sin(yh) + yl * (1 - z * (0.5L - z * (1.0L / 24)));
}

static ld cos_kl(ld yh, ld yl)
{
	ld z = yh * yh;
	return x87_cos(yh) - yl * yh * (1 - z * (1.0L / 6 - z * (1.0L / 120)));
}

ld sinl(ld x)
{
	if (fabsl(x) == INFINITY || x != x)
		return x != x ? x + x : invalidl(x);
	if (fabsl(x) < 0x1p-32L)
		return x == 0 ? x : x - x * 0x1p-70L;
	ld yh, yl;
	switch (rem_pio2l(x, &yh, &yl)) {
	case 0: return sin_kl(yh, yl);
	case 1: return cos_kl(yh, yl);
	case 2: return -sin_kl(yh, yl);
	default: return -cos_kl(yh, yl);
	}
}

ld cosl(ld x)
{
	if (fabsl(x) == INFINITY || x != x)
		return x != x ? x + x : invalidl(x);
	if (fabsl(x) < 0x1p-33L)
		return 1.0L - 0x1p-70L;
	ld yh, yl;
	switch (rem_pio2l(x, &yh, &yl)) {
	case 0: return cos_kl(yh, yl);
	case 1: return -sin_kl(yh, yl);
	case 2: return -cos_kl(yh, yl);
	default: return sin_kl(yh, yl);
	}
}

ld tanl(ld x)
{
	if (fabsl(x) == INFINITY || x != x)
		return x != x ? x + x : invalidl(x);
	if (fabsl(x) < 0x1p-32L)
		return x == 0 ? x : x + x * 0x1p-70L;
	ld yh, yl;
	int n = rem_pio2l(x, &yh, &yl);
	ld t = x87_tan(yh);
	ld tl = yl * (1 + t * t);
	if (!(n & 1))
		return t + tl;
	/* -1/(t + tl) */
	ld inv = -1.0L / t;
	ld pe;
	ld p = two_prod_l(inv, t, &pe);
	ld err = (1.0L + p) + pe;
	return inv + inv * (err + inv * tl);
}

/* ---- inverse trigonometric ---- */

ld atan2l(ld y, ld x)
{
	__asm__("fpatan" : "=t"(x) : "0"(x), "u"(y) : "st(1)");
	return x;
}

ld atanl(ld x)
{
	return atan2l(x, 1.0L);
}

ld asinl(ld x)
{
	if (x != x)
		return x + x;
	if (fabsl(x) > 1)
		return invalidl(x);
	if (fabsl(x) < 0x1p-32L)
		return x == 0 ? x : x + x * 0x1p-70L;
	return atan2l(x, sqrtl((1 - x) * (1 + x)));
}

ld acosl(ld x)
{
	if (x != x)
		return x + x;
	if (fabsl(x) > 1)
		return invalidl(x);
	return atan2l(sqrtl((1 - x) * (1 + x)), x);
}

/* ---- hyperbolic ---- */

/* (e^a + sign e^-a)/2 for a >= 0.3 */
static ld exp_pm_l(ld a, ld sign)
{
	int e;
	ld l;
	ld h = exp_kernel(a, 0, &l, &e);
	if (e > 70)
		return scale_l(h, l, e - 1);
	ld inv = 1.0L / h;
	ld pe;
	ld p = two_prod_l(inv, h, &pe);
	ld invl = inv * (((1.0L - p) - pe) - inv * l);
	ld s1 = pow2l(e), s2 = pow2l(-e);
	ld err;
	ld s = two_sum_l(h * s1, sign * inv * s2, &err);
	return 0.5L * (s + (err + l * s1 + sign * invl * s2));
}

ld sinhl(ld x)
{
	if (x != x || fabsl(x) == INFINITY)
		return x + x;
	ld a = fabsl(x);
	if (a < 0x1p-32L)
		return x == 0 ? x : x + x * 0x1p-70L;
	if (a < 0.3L) {
		/* x + x^3/3! + ... + x^17/17! */
		ld z = x * x;
		ld p = 1.0L / 6 + z * (1.0L / 120 + z * (1.0L / 5040 + z * (1.0L / 362880 + z * (1.0L / 39916800 +
		       z * (1.0L / 6227020800 + z * (1.0L / 1307674368000 + z * (1.0L / 355687428096000)))))));
		return x + x * z * p;
	}
	if (a > 11357.3L)
		return oflowl(x < 0);
	ld r = exp_pm_l(a, -1.0L);
	return x < 0 ? -r : r;
}

ld coshl(ld x)
{
	if (x != x || fabsl(x) == INFINITY)
		return x * x;
	ld a = fabsl(x);
	if (a < 0x1p-33L)
		return 1.0L + 0x1p-70L;
	if (a > 11357.3L)
		return oflowl(0);
	if (a >= 0.3L)
		return exp_pm_l(a, 1.0L);
	int e;
	ld l;
	ld h = exp_kernel(a, 0, &l, &e);
	ld s = pow2l(e);
	h *= s;
	l *= s;
	ld inv = 1.0L / h;
	ld pe;
	ld p = two_prod_l(inv, h, &pe);
	ld invl = inv * (((1.0L - p) - pe) - inv * l);
	ld err;
	ld sum = two_sum_l(h, inv, &err);
	return 0.5L * (sum + (err + l + invl));
}

/* (nh + nl)/(dh + dl) */
static ld div_dd(ld nh, ld nl, ld dh, ld dl)
{
	ld q = nh / dh;
	ld qe;
	ld qd = two_prod_l(q, dh, &qe);
	return q + ((((nh - qd) - qe) + nl - q * dl) / dh);
}

ld tanhl(ld x)
{
	if (x != x)
		return x + x;
	ld a = fabsl(x);
	ld sgn = x < 0 ? -1.0L : 1.0L;
	if (a == INFINITY)
		return sgn;
	if (a < 0x1p-33L)
		return x == 0 ? x : x - x * 0x1p-70L;
	if (a > 24)
		return sgn * (1.0L - 0x1p-70L);
	if (a <= 0.1733L) {
		/* E = expm1(-2a); tanh a = -E/(2 + E) */
		ld el;
		ld eh = expm1_small(-2 * a, &el);
		ld dl;
		ld dh = fast_two_sum_l(2.0L, eh, &dl);
		return -sgn * div_dd(eh, el, dh, dl + el);
	}
	/* (1 - u)/(1 + u), u = e^-2a */
	int e;
	ld l;
	ld h = exp_kernel(-2 * a, 0, &l, &e);
	ld s = pow2l(e);
	ld uh = h * s, ul = l * s;
	ld nl, dl;
	ld n = fast_two_sum_l(1.0L, -uh, &nl);
	ld d = fast_two_sum_l(1.0L, uh, &dl);
	return sgn * div_dd(n, nl - ul, d, dl + ul);
}

/* log(v + vl) */
static ld log_dd2_l(ld v, ld vl)
{
	ld l;
	ld h = log_kernel(v, &l);
	return h + (l + vl / v);
}

ld asinhl(ld x)
{
	if (x != x || fabsl(x) == INFINITY)
		return x + x;
	ld a = fabsl(x);
	ld sgn = x < 0 ? -1.0L : 1.0L;
	if (a < 0x1p-32L)
		return x == 0 ? x : x - x * 0x1p-70L;
	if (a > 0x1p33L) {
		ld l;
		ld h = log_kernel(a, &l);
		ld e;
		ld s = two_sum_l(h, LLN2_H, &e);
		return sgn * (s + (e + l + LLN2_L));
	}
	ld pe;
	ld p = two_prod_l(a, a, &pe);
	ld we;
	ld w = two_sum_l(p, 1.0L, &we);
	we += pe;
	ld s = sqrtl(w);
	ld se;
	ld ss = two_prod_l(s, s, &se);
	ld sl = (((w - ss) - se) + we) / (2 * s);
	if (a < 0.5L) {
		/* log1p(a + a^2/(1 + sqrt(1 + a^2))) */
		ld dl;
		ld d = fast_two_sum_l(1.0L, s, &dl);
		ld t = div_dd(p, pe, d, dl + sl);
		ld tl;
		ld th = fast_two_sum_l(a, t, &tl);
		ld ul;
		ld u = fast_two_sum_l(1.0L, th, &ul);
		return sgn * log_dd2_l(u, ul + tl);
	}
	ld ve;
	ld v = fast_two_sum_l(s, a, &ve);
	return sgn * log_dd2_l(v, ve + sl);
}

ld acoshl(ld x)
{
	if (x != x)
		return x + x;
	if (x < 1)
		return invalidl(x);
	if (x == 1)
		return 0;
	if (x == INFINITY)
		return x;
	if (x > 0x1p33L) {
		ld l;
		ld h = log_kernel(x, &l);
		ld e;
		ld s = two_sum_l(h, LLN2_H, &e);
		return s + (e + l + LLN2_L);
	}
	if (x >= 2) {
		ld pe;
		ld p = two_prod_l(x, x, &pe);
		ld we;
		ld w = fast_two_sum_l(p, -1.0L, &we);
		we += pe;
		ld s = sqrtl(w);
		ld se;
		ld ss = two_prod_l(s, s, &se);
		ld sl = (((w - ss) - se) + we) / (2 * s);
		ld ve;
		ld v = fast_two_sum_l(x, s, &ve);
		return log_dd2_l(v, ve + sl);
	}
	ld t = x - 1; /* exact */
	ld be;
	ld b = fast_two_sum_l(2.0L, t, &be);
	ld ve;
	ld v = two_prod_l(t, b, &ve);
	ve += t * be;
	ld s = sqrtl(v);
	ld se;
	ld ss = two_prod_l(s, s, &se);
	ld sl = (((v - ss) - se) + ve) / (2 * s);
	ld ue;
	ld u = two_sum_l(t, s, &ue);
	ue += sl;
	ld l;
	ld h = log1p_kernel(u, &l);
	return h + (l + ue / (1 + u));
}

ld atanhl(ld x)
{
	if (x != x)
		return x + x;
	ld a = fabsl(x);
	ld sgn = x < 0 ? -1.0L : 1.0L;
	if (a >= 1)
		return a == 1 ? math_divzero(x < 0) : invalidl(x);
	if (a < 0x1p-33L)
		return x == 0 ? x : x + x * 0x1p-70L;
	ld dl;
	ld d = fast_two_sum_l(1.0L, -a, &dl);
	ld n = 2 * a;
	ld q = n / d;
	ld qe;
	ld qd = two_prod_l(q, d, &qe);
	ld ql = (((n - qd) - qe) - q * dl) / d;
	ld l;
	ld h = log1p_kernel(q, &l);
	return sgn * 0.5L * (h + (l + ql / (1 + q)));
}

/* ---- cbrt, hypot ---- */

ld cbrtl(ld x)
{
	if (x != x || x == 0 || fabsl(x) == INFINITY)
		return x + x;
	union ldbits u = { fabsl(x) };
	int adj = 0;
	if ((u.i.se & 0x7fff) == 0) {
		u.f *= 0x1p66L;
		adj = -22;
	}
	int e = (u.i.se & 0x7fff) - 0x3fff;
	int q = (e >= 0 ? e : e - 2) / 3;
	int rem = e - 3 * q;
	u.i.se = (uint16_t)(0x3fff + rem);
	ld m = u.f; /* [1, 8) */
	ld y = cbrt((double)m);
	ld pe;
	ld p = two_prod_l(y, y, &pe);
	ld ce;
	ld c = two_prod_l(p, y, &ce);
	ce += pe * y;
	ld r = (c - m) + ce;
	y = y - r / (3 * p);
	y *= pow2l(q + adj);
	return x < 0 ? -y : y;
}

ld hypotl(ld x, ld y)
{
	ld ax = fabsl(x), ay = fabsl(y);
	if (ax == INFINITY || ay == INFINITY)
		return INFINITY;
	if (ax != ax || ay != ay)
		return x + y;
	if (ay > ax) {
		ld t = ax;
		ax = ay;
		ay = t;
	}
	if (ay == 0)
		return ax;
	int ea = ld_exp(ax), eb = ld_exp(ay);
	if (ea - eb > 70)
		return ax + ay;
	int k = 0;
	if (ea > 0x3fff + 8000) {
		ax *= 0x1p-9000L;
		ay *= 0x1p-9000L;
		k = 9000;
	} else if (eb < 0x3fff - 8000) {
		ax *= 0x1p9000L;
		ay *= 0x1p9000L;
		k = -9000;
	}
	ld e1, e2, e3;
	ld p1 = two_prod_l(ax, ax, &e1);
	ld p2 = two_prod_l(ay, ay, &e2);
	ld s = fast_two_sum_l(p1, p2, &e3);
	ld sl = e3 + e1 + e2;
	ld h = sqrtl(s);
	ld he;
	ld hh = two_prod_l(h, h, &he);
	ld corr = (((s - hh) - he) + sl) / (2 * h);
	if (k == 0)
		return h + corr;
	return scale_l(h, corr, k);
}
