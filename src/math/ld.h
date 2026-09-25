/* Internal: double-long-double helpers and the long double kernels. */
#ifndef CITADEL_LD_H
#define CITADEL_LD_H
#include "libm.h"

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

hidden long double __scale_l(long double hi, long double lo, int e);
hidden long double __exp_kernel_l(long double wh, long double wl, long double *lo, int *e);
hidden long double __log_kernel_l(long double x, long double *lo);
hidden long double __log1p_kernel_l(long double x, long double *lo);

#endif
