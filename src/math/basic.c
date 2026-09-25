/* Exact operations: sign, min/max, rounding, decomposition, nextafter,
 * square roots, fmod and remainder. */
#include "libm.h"
#include <limits.h>

/* ---- sign and comparison ---- */

double fabs(double x) { return asdbl(asu64(x) & ~(1ULL << 63)); }
float fabsf(float x) { return asflt(asu32(x) & 0x7fffffff); }
long double fabsl(long double x) { union ldbits u = { x }; u.i.se &= 0x7fff; return u.f; }

double copysign(double x, double y) { return asdbl((asu64(x) & ~(1ULL << 63)) | (asu64(y) & (1ULL << 63))); }
float copysignf(float x, float y) { return asflt((asu32(x) & 0x7fffffff) | (asu32(y) & 0x80000000)); }
long double copysignl(long double x, long double y)
{
	union ldbits a = { x }, b = { y };
	a.i.se = (uint16_t)((a.i.se & 0x7fff) | (b.i.se & 0x8000));
	return a.f;
}

/* fmax/fmin ignore a single NaN and order -0 below +0 */
#define MINMAX(T, sfx)                                                        \
	T fmax##sfx(T x, T y)                                                 \
	{                                                                     \
		if (x != x) return y;                                         \
		if (y != y) return x;                                         \
		if (x == y) return __builtin_signbit(x) ? y : x;              \
		return x > y ? x : y;                                         \
	}                                                                     \
	T fmin##sfx(T x, T y)                                                 \
	{                                                                     \
		if (x != x) return y;                                         \
		if (y != y) return x;                                         \
		if (x == y) return __builtin_signbit(x) ? x : y;              \
		return x < y ? x : y;                                         \
	}                                                                     \
	T fdim##sfx(T x, T y)                                                 \
	{                                                                     \
		if (x != x || y != y) return x + y;                           \
		return x > y ? x - y : 0;                                     \
	}
MINMAX(double, )
MINMAX(float, f)
MINMAX(long double, l)

double nan(const char *s) { return __builtin_nan(""); }
float nanf(const char *s) { return __builtin_nanf(""); }

/* ---- decomposition ---- */

double frexp(double x, int *e)
{
	uint64_t i = asu64(x);
	int ee = (int)(i >> 52 & 0x7ff);
	if (!ee) {
		if (x == 0) {
			*e = 0;
			return x;
		}
		x = frexp(x * 0x1p64, e);
		*e -= 64;
		return x;
	}
	if (ee == 0x7ff) {
		*e = 0;
		return x;
	}
	*e = ee - 0x3fe;
	return asdbl((i & 0x800fffffffffffffULL) | 0x3fe0000000000000ULL);
}

float frexpf(float x, int *e)
{
	uint32_t i = asu32(x);
	int ee = (int)(i >> 23 & 0xff);
	if (!ee) {
		if (x == 0) {
			*e = 0;
			return x;
		}
		x = frexpf(x * 0x1p64f, e);
		*e -= 64;
		return x;
	}
	if (ee == 0xff) {
		*e = 0;
		return x;
	}
	*e = ee - 0x7e;
	return asflt((i & 0x807fffff) | 0x3f000000);
}

long double frexpl(long double x, int *e)
{
	union ldbits u = { x };
	int ee = u.i.se & 0x7fff;
	if (!ee) {
		if (x == 0) {
			*e = 0;
			return x;
		}
		x = frexpl(x * 0x1p120L, e);
		*e -= 120;
		return x;
	}
	if (ee == 0x7fff) {
		*e = 0;
		return x;
	}
	*e = ee - 0x3ffe;
	u.i.se = (uint16_t)((u.i.se & 0x8000) | 0x3ffe);
	return u.f;
}

/* x * 2^n with a single rounding (steps keep intermediates exact) */
double scalbn(double x, int n)
{
	if (n > 1023) {
		x *= 0x1p1023;
		n -= 1023;
		if (n > 1023) {
			x *= 0x1p1023;
			n -= 1023;
			if (n > 1023)
				n = 1023;
		}
	} else if (n < -1022) {
		/* keep 53 bits of room so only the last multiply rounds */
		x *= 0x1p-1022 * 0x1p53;
		n += 1022 - 53;
		if (n < -1022) {
			x *= 0x1p-1022 * 0x1p53;
			n += 1022 - 53;
			if (n < -1022)
				n = -1022;
		}
	}
	return x * asdbl((uint64_t)(0x3ff + n) << 52);
}

float scalbnf(float x, int n)
{
	return (float)scalbn(x, n < -400 ? -400 : n > 400 ? 400 : n);
}

long double scalbnl(long double x, int n)
{
	if (n > 16383) {
		x *= 0x1p16383L;
		n -= 16383;
		if (n > 16383) {
			x *= 0x1p16383L;
			n -= 16383;
			if (n > 16383)
				n = 16383;
		}
	} else if (n < -16382) {
		x *= 0x1p-16382L * 0x1p64L;
		n += 16382 - 64;
		if (n < -16382) {
			x *= 0x1p-16382L * 0x1p64L;
			n += 16382 - 64;
			if (n < -16382)
				n = -16382;
		}
	}
	union ldbits u;
	u.i.m = 1ULL << 63;
	u.i.se = (uint16_t)(0x3fff + n);
	return x * u.f;
}

double ldexp(double x, int n) { return scalbn(x, n); }
float ldexpf(float x, int n) { return scalbnf(x, n); }
long double ldexpl(long double x, int n) { return scalbnl(x, n); }

int ilogb(double x)
{
	uint64_t i = asu64(x);
	int e = (int)(i >> 52 & 0x7ff);
	if (!e) {
		if (!(i << 1)) {
			force_eval(math_invalid(0));
			return INT_MIN; /* FP_ILOGB0 */
		}
		return -1023 - __builtin_clzll(i << 12);
	}
	if (e == 0x7ff) {
		force_eval(math_invalid(0));
		return i << 12 ? INT_MIN : INT_MAX; /* FP_ILOGBNAN, infinity */
	}
	return e - 0x3ff;
}

double logb(double x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		return math_divzero(1);
	return ilogb(x);
}
float logbf(float x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		return (float)math_divzero(1);
	return (float)ilogb(x);
}
long double logbl(long double x)
{
	if (!isfinite(x))
		return x * x;
	if (x == 0)
		return math_divzero(1);
	int e;
	frexpl(x, &e);
	return e - 1;
}

/* ---- rounding to integers ---- */

double trunc(double x)
{
	uint64_t i = asu64(x);
	int e = (int)(i >> 52 & 0x7ff) - 0x3ff;
	if (e >= 52)
		return x;
	if (e < 0)
		return asdbl(i & (1ULL << 63));
	return asdbl(i & ~((1ULL << (52 - e)) - 1));
}

double floor(double x)
{
	double t = trunc(x);
	return t > x ? t - 1 : t;
}

double ceil(double x)
{
	double t = trunc(x);
	return t < x ? t + 1 : t;
}

double round(double x)
{
	double t = trunc(x);
	double d = fabs(x - t); /* exact */
	if (d >= 0.5)
		t += copysign(1.0, x);
	return t == 0 ? copysign(0.0, x) : t;
}

double rint(double x)
{
	/* adding and subtracting 2^52 rounds in the current mode */
	uint64_t i = asu64(x);
	int e = (int)(i >> 52 & 0x7ff);
	if (e >= 0x3ff + 52)
		return x;
	double big = (i >> 63) ? -0x1p52 : 0x1p52;
	double y = opaque(x + big) - big;
	return y == 0 ? copysign(0.0, x) : y;
}

static unsigned get_mxcsr(void) { unsigned m; __asm__ __volatile__("stmxcsr %0" : "=m"(m)); return m; }
static void set_mxcsr(unsigned m) { __asm__ __volatile__("ldmxcsr %0" : : "m"(m)); }

/* rint without raising inexact */
double nearbyint(double x)
{
	unsigned m = get_mxcsr();
	double r = rint(x);
	set_mxcsr(m);
	return r;
}

long lrint(double x) { long r; __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x)); return r; }
long long llrint(double x) { return lrint(x); }
long lround(double x) { long r; __asm__("cvttsd2si %1, %0" : "=r"(r) : "x"(round(x))); return r; }
long long llround(double x) { return lround(x); }

float truncf(float x)
{
	uint32_t i = asu32(x);
	int e = (int)(i >> 23 & 0xff) - 0x7f;
	if (e >= 23)
		return x;
	if (e < 0)
		return asflt(i & 0x80000000);
	return asflt(i & ~((1u << (23 - e)) - 1));
}
float floorf(float x) { float t = truncf(x); return t > x ? t - 1 : t; }
float ceilf(float x) { float t = truncf(x); return t < x ? t + 1 : t; }
float roundf(float x) { return (float)round(x); }
float rintf(float x) { return (float)rint(x); }
float nearbyintf(float x) { return (float)nearbyint(x); }
long lrintf(float x) { long r; __asm__("cvtss2si %1, %0" : "=r"(r) : "x"(x)); return r; }
long lroundf(float x) { return lround(x); }

long double truncl(long double x)
{
	union ldbits u = { x };
	int e = (u.i.se & 0x7fff) - 0x3fff;
	if (e >= 63)
		return x;
	if (e < 0) {
		u.i.m = 0;
		u.i.se &= 0x8000;
		return u.f;
	}
	u.i.m &= ~((1ULL << (63 - e)) - 1);
	return u.f;
}
long double floorl(long double x) { long double t = truncl(x); return t > x ? t - 1 : t; }
long double ceill(long double x) { long double t = truncl(x); return t < x ? t + 1 : t; }
long double roundl(long double x)
{
	long double t = truncl(x);
	long double d = fabsl(x - t);
	if (d >= 0.5L)
		t += copysignl(1.0L, x);
	return t == 0 ? copysignl(0.0L, x) : t;
}
long double rintl(long double x) { __asm__("frndint" : "+t"(x)); return x; }
long double nearbyintl(long double x)
{
	unsigned short sw;
	long double r = rintl(x);
	__asm__ __volatile__("fnstsw %0" : "=m"(sw));
	/* discard a new inexact flag: clear exceptions only if none were set */
	if (!(sw & 0x3f & ~0x20))
		__asm__ __volatile__("fnclex");
	return r;
}

/* modf: split into integer and fraction, both with x's sign */
double modf(double x, double *ip)
{
	double t = trunc(x);
	*ip = t;
	if (isinf(x))
		return copysign(0.0, x);
	return copysign(x - t, x);
}
float modff(float x, float *ip)
{
	float t = truncf(x);
	*ip = t;
	if (isinf(x))
		return copysignf(0.0f, x);
	return copysignf(x - t, x);
}
long double modfl(long double x, long double *ip)
{
	long double t = truncl(x);
	*ip = t;
	if (isinf(x))
		return copysignl(0.0L, x);
	return copysignl(x - t, x);
}

/* ---- nextafter ---- */

double nextafter(double x, double y)
{
	if (x != x || y != y)
		return x + y;
	if (x == y)
		return y;
	uint64_t i = asu64(x);
	if (x == 0)
		i = (asu64(y) & (1ULL << 63)) | 1;
	else if ((x < y) == !(i >> 63))
		i++;
	else
		i--;
	double r = asdbl(i);
	if (!isfinite(r))
		force_eval(x + x);          /* overflow */
	else if (!isnormal(r))
		force_eval(r * r + x * x);  /* underflow */
	return r;
}

float nextafterf(float x, float y)
{
	if (x != x || y != y)
		return x + y;
	if (x == y)
		return y;
	uint32_t i = asu32(x);
	if (x == 0)
		i = (asu32(y) & 0x80000000) | 1;
	else if ((x < y) == !(i >> 31))
		i++;
	else
		i--;
	return asflt(i);
}

long double nextafterl(long double x, long double y)
{
	if (x != x || y != y)
		return x + y;
	if (x == y)
		return y;
	union ldbits u = { x };
	int up = (x < y) == !(u.i.se >> 15);
	if (x == 0) {
		union ldbits v = { y };
		u.i.m = 1;
		u.i.se = v.i.se & 0x8000;
		return u.f;
	}
	if (up) {
		if (++u.i.m == 0) { /* carry into the exponent */
			u.i.m = 1ULL << 63;
			u.i.se++;
		} else if ((u.i.se & 0x7fff) == 0 && u.i.m == 1ULL << 63) {
			u.i.se++; /* subnormal to normal */
		}
	} else {
		if ((u.i.se & 0x7fff) && u.i.m == 1ULL << 63) {
			u.i.se--;
			u.i.m = (u.i.se & 0x7fff) ? ~0ULL : (~0ULL >> 1);
		} else {
			u.i.m--;
		}
	}
	return u.f;
}

/* ---- square roots (correctly rounded by the hardware) ---- */

double sqrt(double x) { return sqrt_(x); }
float sqrtf(float x) { __asm__("sqrtss %1, %0" : "=x"(x) : "x"(x)); return x; }
long double sqrtl(long double x) { __asm__("fsqrt" : "+t"(x)); return x; }

/* ---- fmod and remainder (exact) ---- */

/* |x| mod |y| for finite x, nonzero finite y; optionally the low bits of
 * the quotient. Integer long division on the significands. */
static double fmod_core(double x, double y, unsigned *quo)
{
	uint64_t ix = asu64(x), iy = asu64(y);
	int ex = (int)(ix >> 52 & 0x7ff), ey = (int)(iy >> 52 & 0x7ff);
	uint64_t mx = ix & ((1ULL << 52) - 1), my = iy & ((1ULL << 52) - 1);
	unsigned q = 0;
	if (!ex) { int s = __builtin_clzll(mx) - 11; mx <<= s; ex = 1 - s; } else mx |= 1ULL << 52;
	if (!ey) { int s = __builtin_clzll(my) - 11; my <<= s; ey = 1 - s; } else my |= 1ULL << 52;
	if (ex < ey || (ex == ey && mx < my)) {
		if (quo)
			*quo = 0;
		return fabs(x);
	}
	for (; ex > ey; ex--) {
		if (mx >= my) {
			mx -= my;
			q++;
		}
		mx <<= 1;
		q <<= 1;
	}
	if (mx >= my) {
		mx -= my;
		q++;
	}
	if (quo)
		*quo = q;
	if (!mx)
		return 0;
	int s = __builtin_clzll(mx) - 11;
	mx <<= s;
	ex -= s;
	if (ex > 0)
		return asdbl((mx & ((1ULL << 52) - 1)) | (uint64_t)ex << 52);
	return asdbl(mx >> (1 - ex));
}

double fmod(double x, double y)
{
	if (x != x || y != y || isinf(x) || y == 0)
		return isnan(x) || isnan(y) ? x + y : math_invalid(x);
	if (isinf(y) || x == 0)
		return x;
	return copysign(fmod_core(x, y, 0), x);
}

double remainder(double x, double y)
{
	if (x != x || y != y || isinf(x) || y == 0)
		return isnan(x) || isnan(y) ? x + y : math_invalid(x);
	if (isinf(y))
		return x;
	unsigned q;
	double r = fmod_core(x, y, &q);
	double ay = fabs(y);
	/* round the quotient to nearest, ties to even */
	if (r > ay - r || (r == ay - r && (q & 1)))
		r -= ay;
	return r == 0 ? copysign(0.0, x) : copysign(1.0, x) * r;
}

/* float results are exact in double */
float fmodf(float x, float y) { return (float)fmod(x, y); }
float remainderf(float x, float y) { return (float)remainder(x, y); }

long double fmodl(long double x, long double y)
{
	unsigned short sw;
	do {
		__asm__("fprem; fnstsw %1" : "+t"(x), "=a"(sw) : "u"(y));
	} while (sw & 0x400);
	return x;
}

long double remainderl(long double x, long double y)
{
	unsigned short sw;
	do {
		__asm__("fprem1; fnstsw %1" : "+t"(x), "=a"(sw) : "u"(y));
	} while (sw & 0x400);
	return x;
}
