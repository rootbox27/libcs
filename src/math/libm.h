/* Internal helpers for the math library. */
#ifndef CITADEL_LIBM_H
#define CITADEL_LIBM_H

#include "../internal.h"
#include <math.h>
#include <stdint.h>

static inline uint64_t asu64(double x) { union { double f; uint64_t i; } u = { x }; return u.i; }
static inline double asdbl(uint64_t i) { union { uint64_t i; double f; } u = { i }; return u.f; }
static inline uint32_t asu32(float x) { union { float f; uint32_t i; } u = { x }; return u.i; }
static inline float asflt(uint32_t i) { union { uint32_t i; float f; } u = { i }; return u.f; }

/* x87 long double: 64-bit significand with explicit integer bit. */
union ldbits {
	long double f;
	struct { uint64_t m; uint16_t se; } i;
};

/* Keep a value (and the exceptions its computation raises) from being
 * optimised away. */
static inline void force_eval(double x) { __asm__ __volatile__("" : : "x"(x)); }
static inline void force_evalf(float x) { __asm__ __volatile__("" : : "x"(x)); }
static inline double opaque(double x) { __asm__("" : "+x"(x)); return x; }

/* Results that raise the right exceptions. */
static inline double math_invalid(double x) { x = opaque(x); return (x - x) / (x - x); }
static inline double math_divzero(int neg) { return (neg ? -1.0 : 1.0) / opaque(0.0); }
static inline double math_oflow(int neg) { double h = opaque(0x1p769); return (neg ? -h : h) * h; }
static inline double math_uflow(int neg) { double t = opaque(0x1p-767); return (neg ? -t : t) * t; }

static inline double sqrt_(double x) { __asm__("sqrtsd %1, %0" : "=x"(x) : "x"(x)); return x; }

/* ---- double-double arithmetic ---- */

/* s + e == a + b exactly */
static inline double two_sum(double a, double b, double *e)
{
	double s = a + b, bb = s - a;
	*e = (a - (s - bb)) + (b - bb);
	return s;
}

/* as two_sum, requiring |a| >= |b| (or a == 0) */
static inline double fast_two_sum(double a, double b, double *e)
{
	double s = a + b;
	*e = b - (s - a);
	return s;
}

/* p + e == a * b exactly (no overflow; Veltkamp/Dekker) */
static inline double two_prod(double a, double b, double *e)
{
	const double c = 0x1p27 + 1;
	double p = a * b;
	double ca = c * a, ah = ca - (ca - a), al = a - ah;
	double cb = c * b, bh = cb - (cb - b), bl = b - bh;
	*e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
	return p;
}

/* shared kernels */
hidden double __exp_dd(double hi, double lo, int *ok);
hidden double __log_dd(double x, double *lo);
hidden int __rem_pio2(double x, double *y);
hidden double __sin_k(double x, double y);
hidden double __cos_k(double x, double y);
hidden double __tan_k(double x, double y, int odd);
hidden double __expm1_core(double x);
hidden double __log1p_core(double x);

#endif
