/* Complex functions, instantiated for double and long double by
 * complex.c. Before including, define:
 *   T, CT        the real and complex types
 *   F(name)      the real function of type T (sin or sinl)
 *   C(name)      the complex function being defined (csin or csinl)
 *   T_EPS, T_MAX, T_MANT, T_MIN_EXP, T_MAX_EXP, LOG_MAX, SPLIT, SCALE_BIG,
 *   TANH_BIG, LN2_HI, LN2_LO, PIO2_HI, PIO2_LO, M_E_
 *
 * The special values are those of C11 Annex G. The inverse functions
 * follow Hull, Fairgrieve and Tang, "Implementing the complex arcsine and
 * arccosine functions using exception handling" (as FreeBSD's catrig.c
 * does); clog, csqrt and catanh form |z|^2 in double-T arithmetic so that
 * nothing cancels catastrophically near the unit circle. */

static inline CT C(mk)(T re, T im)
{
	return __builtin_complex(re, im);
}

static inline void C(raise_inexact)(void)
{
	volatile T one = 1, tiny = T_EPS * T_EPS;
	(void)(one + tiny);
}

/* a*b = *p + *e exactly (Dekker; no fma needed) */
static inline void C(two_prod)(T a, T b, T *p, T *e)
{
	*p = a * b;
	T c = SPLIT * a, ah = c - (c - a), al = a - ah;
	c = SPLIT * b;
	T bh = c - (c - b), bl = b - bh;
	*e = ((ah * bh - *p) + ah * bl + al * bh) + al * bl;
}

static inline void C(two_sum)(T a, T b, T *s, T *e)
{
	*s = a + b;
	T bb = *s - a;
	*e = (a - (*s - bb)) + (b - bb);
}

/* a*a + b*b as hi + lo; no overflow assumed */
static inline void C(sumsq)(T a, T b, T *hi, T *lo)
{
	T h1, l1, h2, l2, e;
	C(two_prod)(a, a, &h1, &l1);
	C(two_prod)(b, b, &h2, &l2);
	C(two_sum)(h1, h2, hi, &e);
	T s = *hi + (e + l1 + l2);
	*lo = (e + l1 + l2) - (s - *hi);
	*hi = s;
}

/* a*a + b*b - 1, accurate even when it cancels */
static inline T C(sumsq_m1)(T a, T b)
{
	T h1, l1, h2, l2, x, ex, y, ey;
	if (a < b) {
		T t = a;
		a = b;
		b = t;
	}
	C(two_prod)(a, a, &h1, &l1);
	C(two_prod)(b, b, &h2, &l2);
	C(two_sum)(h1, -1, &x, &ex);
	C(two_sum)(x, h2, &y, &ey);
	return y + (ey + ex + l1 + l2);
}

/* ---- simple ones ---- */

T C(abs)(CT z)
{
	return F(hypot)(__real__ z, __imag__ z);
}

T C(arg)(CT z)
{
	return F(atan2)(__imag__ z, __real__ z);
}

T C(real)(CT z)
{
	return __real__ z;
}

T C(imag)(CT z)
{
	return __imag__ z;
}

CT C(onj)(CT z)
{
	return C(mk)(__real__ z, -__imag__ z);
}

CT C(proj)(CT z)
{
	if (__builtin_isinf(__real__ z) || __builtin_isinf(__imag__ z))
		return C(mk)(__builtin_inf(), F(copysign)(0, __imag__ z));
	return z;
}

/* ---- exponential and logarithm ---- */

/* exp(ax) * factor * (cos y, sin y) for LOG_MAX <= ax < 2 LOG_MAX, where
 * exp(ax) alone overflows but the product may not */
static void C(exp_scaled)(T ax, T y, T factor, T *re, T *im)
{
	/* exp(ax) = 2^k exp(r) with |r| <= ln2/2; k * LN2_HI is exact and
	 * so is the subtraction, so r carries no rounding error that exp
	 * would magnify */
	int k = (int)(ax / (LN2_HI + LN2_LO) + (T)0.5);
	T e = F(exp)((ax - k * LN2_HI) - k * LN2_LO) * factor;
	/* half the scaling first, so a tiny sin y (a subnormal y) does not
	 * make the product subnormal and lose its precision */
	e = F(scalbn)(e, k / 2);
	*re = F(scalbn)(e * F(cos)(y), k - k / 2);
	*im = F(scalbn)(e * F(sin)(y), k - k / 2);
}

CT C(exp)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	if (y == 0)
		return C(mk)(F(exp)(x), y);
	if (__builtin_isinf(x)) {
		if (x < 0) {
			if (!__builtin_isfinite(y))
				return C(mk)(0, F(copysign)(0, y));
			return C(mk)(0 * F(cos)(y), 0 * F(sin)(y));
		}
		if (!__builtin_isfinite(y))
			return C(mk)(x, y - y);
		return C(mk)(x * F(cos)(y), x * F(sin)(y));
	}
	if (!__builtin_isfinite(y) || __builtin_isnan(x))
		return C(mk)(y - y + x, y - y + x);
	if (x >= LOG_MAX) {
		T re, im;
		if (x >= 2 * LOG_MAX) {
			volatile T huge = T_MAX;
			T h = huge * 2;
			return C(mk)(h * F(cos)(y), h * F(sin)(y));
		}
		C(exp_scaled)(x, y, 1, &re, &im);
		return C(mk)(re, im);
	}
	T r = F(exp)(x);
	return C(mk)(r * F(cos)(y), r * F(sin)(y));
}

CT C(log)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	if (__builtin_isnan(x) || __builtin_isnan(y)) {
		if (__builtin_isinf(x) || __builtin_isinf(y))
			return C(mk)(__builtin_inf(), x + y);
		return C(mk)(x + y, x + y);
	}
	T im = F(atan2)(y, x);
	if (__builtin_isinf(x) || __builtin_isinf(y))
		return C(mk)(__builtin_inf(), im);
	T ax = F(fabs)(x), ay = F(fabs)(y);
	if (ax < ay) {
		T t = ax;
		ax = ay;
		ay = t;
	}
	if (ax == 0)
		return C(mk)(-1 / ax, im);
	/* bring |z|^2 into range: z = 2^k (ax + i ay) */
	int k = 0;
	if (ax > SCALE_BIG || ax < 1 / SCALE_BIG) {
		k = F(ilogb)(ax);
		ax = F(scalbn)(ax, -k);
		ay = F(scalbn)(ay, -k);
	}
	T h, l;
	C(sumsq)(ax, ay, &h, &l);
	if (!k && h >= (T)0.5 && h <= 2)
		return C(mk)(F(log1p)(C(sumsq_m1)(ax, ay)) / 2, im);
	T re = F(log)(h) / 2 + l / (2 * h);
	if (k)
		re = k * LN2_HI + (re + k * LN2_LO);
	return C(mk)(re, im);
}

CT C(pow)(CT z, CT w)
{
	return C(exp)(w * C(log)(z));
}

CT C(sqrt)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	if (__builtin_isinf(y))
		return C(mk)(__builtin_inf(), y);
	if (__builtin_isnan(x))
		return C(mk)(x, y - y + x);
	if (__builtin_isinf(x)) {
		if (__builtin_isnan(y))
			return x > 0 ? C(mk)(x, y) : C(mk)(y, x);
		return x > 0 ? C(mk)(x, F(copysign)(0, y)) : C(mk)(0, F(copysign)(-x, y));
	}
	if (__builtin_isnan(y))
		return C(mk)(y, y);
	if (x == 0 && y == 0)
		return C(mk)(0, y);
	T ax = F(fabs)(x), ay = F(fabs)(y), ay0 = ay;
	T m = ax > ay ? ax : ay;
	/* z = 4^k (ax + i ay), so sqrt z = 2^k sqrt(ax + i ay) */
	int k = 0;
	if (m > SCALE_BIG || m < 1 / SCALE_BIG) {
		k = F(ilogb)(m) / 2;
		ax = F(scalbn)(ax, -2 * k);
		ay = F(scalbn)(ay, -2 * k);
	}
	/* |z| = r + rl */
	T h, l, p, e;
	C(sumsq)(ax, ay, &h, &l);
	T r = F(sqrt)(h);
	C(two_prod)(r, r, &p, &e);
	T rl = (((h - p) - e) + l) / (2 * r);
	/* (|x| + |z|) / 2 = u + ul */
	T u, ul;
	C(two_sum)(ax, r, &u, &ul);
	ul += rl;
	u /= 2;
	ul /= 2;
	/* t + tl = sqrt(u + ul) */
	T t = F(sqrt)(u);
	C(two_prod)(t, t, &p, &e);
	T tl = (((u - p) - e) + ul) / (2 * t);
	if (k) {
		t = F(scalbn)(t, k);
		tl = F(scalbn)(tl, k);
	}
	T big = t + tl;
	/* the other part is |y| / (2 (t + tl)), divided with an exact
	 * remainder; the unscaled |y| keeps it out of the subnormals */
	T q = ay0 / (2 * t), small = q;
	if (__builtin_isfinite(q) && q != 0) {
		C(two_prod)(q, 2 * t, &p, &e);
		small = q + (((ay0 - p) - e) - q * 2 * tl) / (2 * t);
	}
	if (__builtin_signbit(x))
		return C(mk)(small, F(copysign)(big, y));
	return C(mk)(big, F(copysign)(small, y));
}

/* ---- hyperbolic and circular functions ---- */

CT C(cosh)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	int fx = __builtin_isfinite(x), fy = __builtin_isfinite(y);
	if (fx && fy) {
		if (y == 0)
			return C(mk)(F(cosh)(x), x * y);
		T ax = F(fabs)(x);
		if (ax < LOG_MAX)
			return C(mk)(F(cosh)(x) * F(cos)(y), F(sinh)(x) * F(sin)(y));
		if (ax < 2 * LOG_MAX) {
			T re, im;
			C(exp_scaled)(ax, y, (T)0.5, &re, &im);
			return C(mk)(re, F(copysign)(1, x) * im);
		}
		volatile T huge = T_MAX;
		T h = huge * x;
		return C(mk)(h * h * F(cos)(y), h * F(sin)(y));
	}
	if (x == 0)
		return C(mk)(y - y, F(copysign)(0, x * (y - y)));
	if (y == 0)
		return C(mk)(x * x, F(copysign)(0, x) * y);
	if (fx)
		return C(mk)(y - y, x * (y - y));
	if (__builtin_isinf(x)) {
		if (!fy)
			return C(mk)(x * x, x * (y - y));
		return C(mk)((x * x) * F(cos)(y), x * F(sin)(y));
	}
	return C(mk)((x * x) * (y - y), (x + x) * (y - y));
}

CT C(sinh)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	int fx = __builtin_isfinite(x), fy = __builtin_isfinite(y);
	if (fx && fy) {
		if (y == 0)
			return C(mk)(F(sinh)(x), y);
		T ax = F(fabs)(x);
		if (ax < LOG_MAX)
			return C(mk)(F(sinh)(x) * F(cos)(y), F(cosh)(x) * F(sin)(y));
		if (ax < 2 * LOG_MAX) {
			T re, im;
			C(exp_scaled)(ax, y, (T)0.5, &re, &im);
			return C(mk)(F(copysign)(1, x) * re, im);
		}
		volatile T huge = T_MAX;
		T h = huge * x;
		return C(mk)(h * F(cos)(y), h * h * F(sin)(y));
	}
	if (x == 0)
		return C(mk)(x, y - y);
	if (y == 0)
		return C(mk)(x, y);
	if (fx)
		return C(mk)(y - y, y - y);
	if (__builtin_isinf(x)) {
		if (!fy)
			return C(mk)(x * x, x * (y - y));
		return C(mk)(x * F(cos)(y), __builtin_inf() * F(sin)(y));
	}
	return C(mk)((x * x) * (y - y), (x + x) * (y - y));
}

CT C(tanh)(CT z)
{
	T x = __real__ z, y = __imag__ z;
	if (!__builtin_isfinite(x)) {
		if (__builtin_isnan(x))
			return C(mk)(x + y, y == 0 ? y : x + y);
		return C(mk)(F(copysign)(1, x),
		             F(copysign)(0, __builtin_isinf(y) ? y : F(sin)(y) * F(cos)(y)));
	}
	if (!__builtin_isfinite(y))
		return C(mk)(x == 0 ? x : y - y, y - y);
	if (F(fabs)(x) >= TANH_BIG) {
		T e = F(exp)(-F(fabs)(x));
		return C(mk)(F(copysign)(1, x), 4 * F(sin)(y) * F(cos)(y) * e * e);
	}
	/* Kahan: tanh(x+iy) = (beta rho s + i t) / (1 + beta s^2) */
	T t = F(tan)(y), beta = 1 + t * t, s = F(sinh)(x), rho = F(sqrt)(1 + s * s);
	T den = 1 + beta * s * s;
	return C(mk)((beta * rho * s) / den, t / den);
}

/* the circular functions via the hyperbolic ones: sin z = -i sinh(iz),
 * cos z = cosh(iz), tan z = -i tanh(iz) */
CT C(sin)(CT z)
{
	CT w = C(sinh)(C(mk)(-__imag__ z, __real__ z));
	return C(mk)(__imag__ w, -__real__ w);
}

CT C(cos)(CT z)
{
	return C(cosh)(C(mk)(-__imag__ z, __real__ z));
}

CT C(tan)(CT z)
{
	CT w = C(tanh)(C(mk)(-__imag__ z, __real__ z));
	return C(mk)(__imag__ w, -__real__ w);
}

/* ---- inverse functions ---- */

#define A_CROSSOVER 10
#define B_CROSSOVER ((T)0.6417)
#define RECIP_EPS (1 / T_EPS)
#define SQRT_6_EPS F(sqrt)(6 * T_EPS)
#define SQRT_3_EPS F(sqrt)(3 * T_EPS)
#define FOUR_SQRT_MIN F(scalbn)(1, (T_MIN_EXP - 1) / 2 + 2)
#define SQRT_MIN F(scalbn)(1, (T_MIN_EXP - 1) / 2)
#define QUARTER_SQRT_MAX F(scalbn)(1, T_MAX_EXP / 2 - 2)

static inline T C(fhf)(T a, T b, T hyp)
{
	if (b < 0)
		return (hyp - b) / 2;
	if (b == 0)
		return a / 2;
	return a * a / (hyp + b) / 2;
}

static void C(hard_work)(T x, T y, T *rx, int *b_usable, T *b, T *sqrt_a2my2, T *new_y)
{
	T r = F(hypot)(x, y + 1), s = F(hypot)(x, y - 1);
	T a = (r + s) / 2;
	if (a < 1)
		a = 1;
	if (a < A_CROSSOVER) {
		if (y == 1 && x < T_EPS * T_EPS / 128)
			*rx = F(sqrt)(x);
		else if (x >= T_EPS * F(fabs)(y - 1))
		{
			T am1 = C(fhf)(x, 1 + y, r) + C(fhf)(x, 1 - y, s);
			*rx = F(log1p)(am1 + F(sqrt)(am1 * (a + 1)));
		} else if (y < 1)
			*rx = x / F(sqrt)((1 - y) * (1 + y));
		else
			*rx = F(log1p)((y - 1) + F(sqrt)((y - 1) * (y + 1)));
	} else {
		*rx = F(log)(a + F(sqrt)(a * a - 1));
	}
	*new_y = y;
	if (y < FOUR_SQRT_MIN) {
		*b_usable = 0;
		*sqrt_a2my2 = a * (2 / T_EPS);
		*new_y = y * (2 / T_EPS);
		return;
	}
	*b = y / a;
	*b_usable = 1;
	if (*b > B_CROSSOVER) {
		*b_usable = 0;
		if (y == 1 && x < T_EPS / 128) {
			*sqrt_a2my2 = F(sqrt)(x) * F(sqrt)((a + y) / 2);
		} else if (x >= T_EPS * F(fabs)(y - 1)) {
			T amy = C(fhf)(x, y + 1, r) + C(fhf)(x, y - 1, s);
			*sqrt_a2my2 = F(sqrt)(amy * (a + y));
		} else if (y > 1) {
			*sqrt_a2my2 = x * (4 / T_EPS / T_EPS) * y / F(sqrt)((y + 1) * (y - 1));
			*new_y = y * (4 / T_EPS / T_EPS);
		} else {
			*sqrt_a2my2 = F(sqrt)((1 - y) * (1 + y));
		}
	}
}

/* log(z) for |z| beyond RECIP_EPS: z is huge, no cancellation possible */
static CT C(log_large)(T x, T y)
{
	T ax = F(fabs)(x), ay = F(fabs)(y);
	if (ax < ay) {
		T t = ax;
		ax = ay;
		ay = t;
	}
	if (ax > T_MAX / 2)
		return C(mk)(F(log)(F(hypot)(x / M_E_, y / M_E_)) + 1, F(atan2)(y, x));
	if (ax > QUARTER_SQRT_MAX || ay < SQRT_MIN)
		return C(mk)(F(log)(F(hypot)(x, y)), F(atan2)(y, x));
	return C(mk)(F(log)(ax * ax + ay * ay) / 2, F(atan2)(y, x));
}

CT C(asinh)(CT z)
{
	T x = __real__ z, y = __imag__ z, ax = F(fabs)(x), ay = F(fabs)(y);
	if (__builtin_isnan(x) || __builtin_isnan(y)) {
		if (__builtin_isinf(x))
			return C(mk)(x, y + y);
		if (__builtin_isinf(y))
			return C(mk)(y, x + x);
		if (y == 0)
			return C(mk)(x + x, y);
		return C(mk)(x + y, x + y);
	}
	if (ax > RECIP_EPS || ay > RECIP_EPS) {
		CT w = __builtin_signbit(x) ? C(log_large)(-x, -y) : C(log_large)(x, y);
		return C(mk)(F(copysign)(__real__ w + LN2_HI + LN2_LO, x), F(copysign)(__imag__ w, y));
	}
	if (x == 0 && y == 0)
		return z;
	/* on the axes the real functions are exact to the last bit */
	if (y == 0)
		return C(mk)(F(asinh)(x), y);
	if (x == 0) {
		if (ay <= 1)
			return C(mk)(x, F(asin)(y));
		return C(mk)(F(copysign)(F(acosh)(ay), x), F(copysign)(PIO2_HI + PIO2_LO, y));
	}
	C(raise_inexact)();
	if (ax < SQRT_6_EPS / 4 && ay < SQRT_6_EPS / 4)
		return z;
	T rx, ry, b, sq, ny;
	int bu;
	C(hard_work)(ax, ay, &rx, &bu, &b, &sq, &ny);
	ry = bu ? F(asin)(b) : F(atan2)(ny, sq);
	return C(mk)(F(copysign)(rx, x), F(copysign)(ry, y));
}

CT C(asin)(CT z)
{
	CT w = C(asinh)(C(mk)(__imag__ z, __real__ z));
	return C(mk)(__imag__ w, __real__ w);
}

CT C(acos)(CT z)
{
	T x = __real__ z, y = __imag__ z, ax = F(fabs)(x), ay = F(fabs)(y);
	int sx = __builtin_signbit(x), sy = __builtin_signbit(y);
	if (__builtin_isnan(x) || __builtin_isnan(y)) {
		if (__builtin_isinf(x))
			return C(mk)(y + y, -__builtin_inf());
		if (__builtin_isinf(y))
			return C(mk)(x + x, -y);
		if (x == 0)
			return C(mk)(PIO2_HI + PIO2_LO, y + y);
		return C(mk)(x + y, x + y);
	}
	if (ax > RECIP_EPS || ay > RECIP_EPS) {
		CT w = C(log_large)(x, y);
		T rx = F(fabs)(__imag__ w), ry = __real__ w + LN2_HI + LN2_LO;
		return C(mk)(rx, sy ? ry : -ry);
	}
	if (x == 1 && y == 0)
		return C(mk)(0, -y);
	if (y == 0) {
		if (ax <= 1)
			return C(mk)(F(acos)(x), -y);
		T im = -F(copysign)(F(acosh)(ax), y);
		return C(mk)(sx ? 2 * PIO2_HI + 2 * PIO2_LO : 0, im);
	}
	if (x == 0)
		return C(mk)(PIO2_HI + PIO2_LO, -F(asinh)(y));
	C(raise_inexact)();
	if (ax < SQRT_6_EPS / 4 && ay < SQRT_6_EPS / 4)
		return C(mk)(PIO2_HI - (x - PIO2_LO), -y);
	T rx, ry, b, sq, nx;
	int bu;
	C(hard_work)(ay, ax, &ry, &bu, &b, &sq, &nx);
	if (bu)
		rx = F(acos)(sx ? -b : b);
	else
		rx = F(atan2)(sq, sx ? -nx : nx);
	return C(mk)(rx, sy ? ry : -ry);
}

CT C(acosh)(CT z)
{
	CT w = C(acos)(z);
	T rx = __real__ w, ry = __imag__ w;
	if (__builtin_isnan(rx) && __builtin_isnan(ry))
		return C(mk)(ry, rx);
	if (__builtin_isnan(rx))
		return C(mk)(F(fabs)(ry), rx);
	if (__builtin_isnan(ry))
		return C(mk)(ry, rx); /* cacosh(+-0 + iNaN) = NaN + i pi/2, as glibc */
	return C(mk)(F(fabs)(ry), F(copysign)(rx, __imag__ z));
}

/* Re(1/(x+iy)) without spurious overflow or underflow */
static T C(re_recip)(T x, T y)
{
	int ex, ey;
	if (__builtin_isinf(x))
		return 1 / x;
	if (__builtin_isinf(y))
		return F(copysign)(0, x);
	F(frexp)(x, &ex);
	F(frexp)(y, &ey);
	if (ex - ey >= T_MANT / 2 + 1)
		return 1 / x;
	if (ey - ex >= T_MANT / 2 + 1)
		return x / y / y;
	if (ex <= T_MAX_EXP / 2 - T_MANT / 2 - 1)
		return x / (x * x + y * y);
	x = F(scalbn)(x, -ex);
	y = F(scalbn)(y, -ex);
	return F(scalbn)(x / (x * x + y * y), -ex);
}

CT C(atanh)(CT z)
{
	T x = __real__ z, y = __imag__ z, ax = F(fabs)(x), ay = F(fabs)(y);
	if (y == 0 && ax <= 1)
		return C(mk)(F(atanh)(x), y);
	if (x == 0)
		return C(mk)(x, F(atan)(y));
	if (__builtin_isnan(x) || __builtin_isnan(y)) {
		if (__builtin_isinf(x))
			return C(mk)(F(copysign)(0, x), y + y);
		if (__builtin_isinf(y))
			return C(mk)(F(copysign)(0, x), F(copysign)(PIO2_HI + PIO2_LO, y));
		return C(mk)(x + y, x + y);
	}
	if (ax > RECIP_EPS || ay > RECIP_EPS)
		return C(mk)(C(re_recip)(x, y), F(copysign)(PIO2_HI + PIO2_LO, y));
	if (ax < SQRT_3_EPS / 2 && ay < SQRT_3_EPS / 2) {
		C(raise_inexact)();
		return z;
	}
	T rx, ry;
	if (ax == 1 && ay < T_EPS) {
		rx = ((LN2_HI + LN2_LO) - F(log)(ay)) / 2;
	} else {
		T d = (ax - 1) * (ax - 1);
		if (ay >= SQRT_MIN)
			d += ay * ay;
		rx = F(log1p)(4 * ax / d) / 4;
	}
	if (ax == 1)
		ry = F(atan2)(2, -ay) / 2;
	else if (ay < T_EPS)
		ry = F(atan2)(2 * ay, (1 - ax) * (1 + ax)) / 2;
	else
		ry = F(atan2)(2 * ay, -C(sumsq_m1)(ax, ay)) / 2;
	return C(mk)(F(copysign)(rx, x), F(copysign)(ry, y));
}

CT C(atan)(CT z)
{
	CT w = C(atanh)(C(mk)(__imag__ z, __real__ z));
	return C(mk)(__imag__ w, __real__ w);
}

#undef A_CROSSOVER
#undef B_CROSSOVER
#undef RECIP_EPS
#undef SQRT_6_EPS
#undef SQRT_3_EPS
#undef FOUR_SQRT_MIN
#undef SQRT_MIN
#undef QUARTER_SQRT_MAX
