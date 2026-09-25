/* erfl, erfcl, lgammal, tgammal: the double algorithms (see erf.c and
 * gamma.c) in double-long-double arithmetic with long double tables;
 * erfc above 27.5 uses its asymptotic series, which converges to below
 * 2^-70 there. */
#include "ld.h"
#include "tables.h"

/* ---- erf, erfc ---- */

static ld erf_small_l(ld x, ld *lo)
{
	ld ze;
	ld z = two_prod_l(x, x, &ze);
	ld z2e;
	ld z2 = two_prod_l(z, z, &z2e);
	z2e += 2 * z * ze;
	ld r = LERF_R0 + z * (LERF_R1 + z * (LERF_R2 + z * (LERF_R3 + z * (LERF_R4 + z * (LERF_R5 + z * (LERF_R6 +
	       z * (LERF_R7 + z * (LERF_R8 + z * (LERF_R9 + z * (LERF_R10 + z * (LERF_R11 + z * (LERF_R12 +
	       z * LERF_R13))))))))))));
	ld ae;
	ld a = two_prod_l(z, LERF_P0_H, &ae);
	ae += z * LERF_P0_L + ze * LERF_P0_H;
	ld be;
	ld b = two_prod_l(z2, LERF_P1_H, &be);
	be += z2 * LERF_P1_L + z2e * LERF_P1_H;
	ld e1, e2;
	ld s = fast_two_sum_l(LERF_C_H, a, &e1);
	s = fast_two_sum_l(s, b, &e2);
	ld sl = LERF_C_L + e1 + e2 + ae + be + z2 * z * r;
	s = fast_two_sum_l(s, sl, &sl);
	ld pe;
	ld p = two_prod_l(x, s, &pe);
	return fast_two_sum_l(p, pe + x * sl, lo);
}

/* erfc(x) = 2^*e (hi + *lo) for x >= 1, x < 107 */
static ld erfc_mid_l(ld x, ld *lo, int *e)
{
	ld wh, wl;
	if (x < 27.5L) {
		int i = 0;
		while (i < ERFC_NINT - 1 && x >= erfc_edges[i + 1])
			i++;
		const ld *row = &lerfc_tab[i * LERFC_ROW];
		ld t = x - row[0];
		int deg = (int)row[5];
		ld rr = row[6 + deg];
		for (int k = deg - 1; k >= 0; k--)
			rr = row[6 + k] + t * rr;
		ld ae;
		ld a = two_prod_l(t, row[3], &ae);
		ae += t * row[4];
		ld e1;
		wh = two_sum_l(row[1], a, &e1);
		wl = row[2] + e1 + ae + t * t * rr;
		wh = fast_two_sum_l(wh, wl, &wl);
	} else {
		/* w(x) = (1/(x sqrt(pi))) (1 - v + 3v^2 - 15v^3 + ...), v = 1/(2x^2) */
		ld v = 1 / (2 * x * x);
		ld s = v * (-1 + v * (3 + v * (-15 + v * (105 + v * (-945 + v * (10395 + v * (-135135 + v * (2027025 +
		       v * (-34459425 + v * 654729075)))))))));
		/* C/(2x) (1 + s), C = 2/sqrt(pi) */
		ld q = LERF_C_H / (2 * x);
		ld qe;
		ld qd = two_prod_l(q, 2 * x, &qe);
		ld ql = (((LERF_C_H - qd) - qe) + LERF_C_L) / (2 * x);
		wh = fast_two_sum_l(q, ql + q * s, &wl);
	}
	ld qe;
	ld q = two_prod_l(x, x, &qe);
	ld el;
	ld eh = __exp_kernel_l(-q, -qe, &el, e);
	ld pe;
	ld p = two_prod_l(eh, wh, &pe);
	return fast_two_sum_l(p, pe + eh * wl + el * wh, lo);
}

ld erfl(ld x)
{
	if (x != x)
		return x + x;
	ld a = fabsl(x), sgn = x < 0 ? -1.0L : 1.0L;
	if (a == INFINITY)
		return sgn;
	if (a < 0x1p-65L)
		return x * LERF_C_H + x * LERF_C_L;
	ld lo;
	if (a < 1) {
		ld hi = erf_small_l(x, &lo);
		return hi + lo;
	}
	if (a >= 7)
		return sgn * (1.0L - 0x1p-80L);
	int e;
	ld h = erfc_mid_l(a, &lo, &e);
	ld s = pow2l(e);
	ld ce;
	ld c = two_sum_l(1.0L, -h * s, &ce);
	return sgn * (c + (ce - lo * s));
}

ld erfcl(ld x)
{
	if (x != x)
		return x + x;
	if (fabsl(x) == INFINITY)
		return x < 0 ? 2.0L : 0.0L;
	if (fabsl(x) < 0x1p-65L)
		return 1.0L - x;
	ld lo;
	if (fabsl(x) < 1) {
		ld hi = erf_small_l(x, &lo);
		ld ce;
		ld c = two_sum_l(1.0L, -hi, &ce);
		return c + (ce - lo);
	}
	if (x >= 107)
		return math_uflow(0);
	if (x <= -7)
		return 2.0L - 0x1p-80L;
	int e;
	ld h = erfc_mid_l(fabsl(x), &lo, &e);
	if (x > 0)
		return __scale_l(h, lo, e);
	ld s = pow2l(e);
	ld ce;
	ld c = fast_two_sum_l(2.0L, -h * s, &ce);
	return c + (ce - lo * s);
}

/* ---- lgamma, tgamma ---- */

static ld mul_ddl(ld ah, ld al, ld bh, ld bl, ld *lo)
{
	ld pe;
	ld p = two_prod_l(ah, bh, &pe);
	return fast_two_sum_l(p, pe + ah * bl + al * bh, lo);
}

static ld horner_dl(const ld *c, int n, ld t, ld *dp)
{
	ld p = c[n - 1], d = 0;
	for (int k = n - 2; k >= 0; k--) {
		d = p + t * d;
		p = c[k] + t * p;
	}
	*dp = d;
	return p;
}

static const ld llgq[] = { LLGAM_Q2, LLGAM_Q3, LLGAM_Q4, LLGAM_Q5, LLGAM_Q6, LLGAM_Q7, LLGAM_Q8, LLGAM_Q9,
	LLGAM_Q10, LLGAM_Q11, LLGAM_Q12, LLGAM_Q13, LLGAM_Q14, LLGAM_Q15, LLGAM_Q16, LLGAM_Q17, LLGAM_Q18,
	LLGAM_Q19, LLGAM_Q20, LLGAM_Q21, LLGAM_Q22, LLGAM_Q23, LLGAM_Q24 };

static ld lg_poly_l(ld t, ld tl, ld *lo)
{
	ld dq;
	ld q = horner_dl(llgq, sizeof llgq / sizeof llgq[0], t, &dq);
	ld ae;
	ld a = two_prod_l(t, LLGAM_C0_H, &ae);
	ae += t * LLGAM_C0_L;
	ld tte;
	ld tt = two_prod_l(t, t, &tte);
	ld be;
	ld b = two_prod_l(tt, LLGAM_Q0, &be);
	be += tt * LLGAM_Q0_L + tte * LLGAM_Q0;
	ld t3e;
	ld t3 = two_prod_l(tt, t, &t3e);
	t3e += tte * t;
	ld ce;
	ld c = two_prod_l(t3, LLGAM_Q1, &ce);
	ce += t3 * LLGAM_Q1_L + t3e * LLGAM_Q1;
	ld e1, e2;
	ld hi = two_sum_l(a, b, &e1);
	hi = two_sum_l(hi, c, &e2);
	ld dig = LLGAM_C0_H + t * (2 * LLGAM_Q0 + t * (3 * LLGAM_Q1 + t * (4 * q + t * dq)));
	ld l = e1 + e2 + ae + be + ce + t3 * t * q + tl * dig;
	return fast_two_sum_l(hi, l, lo);
}

static const ld ltgr[] = { LTGAM_R1, LTGAM_R2, LTGAM_R3, LTGAM_R4, LTGAM_R5, LTGAM_R6, LTGAM_R7, LTGAM_R8,
	LTGAM_R9, LTGAM_R10, LTGAM_R11, LTGAM_R12, LTGAM_R13, LTGAM_R14, LTGAM_R15, LTGAM_R16, LTGAM_R17,
	LTGAM_R18, LTGAM_R19, LTGAM_R20, LTGAM_R21, LTGAM_R22 };

static ld g_poly_l(ld t, ld tl, ld *lo)
{
	ld dr;
	ld r = horner_dl(ltgr, sizeof ltgr / sizeof ltgr[0], t, &dr);
	ld ae;
	ld a = two_prod_l(t, LTGAM_G1_H, &ae);
	ae += t * LTGAM_G1_L;
	ld tte;
	ld tt = two_prod_l(t, t, &tte);
	ld be;
	ld b = two_prod_l(tt, LTGAM_R0, &be);
	be += tte * LTGAM_R0;
	ld e1, e2;
	ld hi = two_sum_l(LTGAM_G0_H, a, &e1);
	hi = two_sum_l(hi, b, &e2);
	ld deriv = LTGAM_G1_H + t * (2 * LTGAM_R0 + t * (3 * r + t * dr));
	ld l = LTGAM_G0_L + e1 + e2 + ae + be + tt * t * r + tl * deriv;
	return fast_two_sum_l(hi, l, lo);
}

static ld log_ddl(ld h, ld l, ld *lo)
{
	ld ll;
	ld lh = __log_kernel_l(h, &ll);
	return fast_two_sum_l(lh, ll + l / h, lo);
}

/* lgamma(xh + xl), xh >= 16 */
static ld stirling_l(ld xh, ld xl, ld *lo)
{
	ld ll;
	ld lx = log_ddl(xh, xl, &ll);
	if (xh > 0x1p16000L) {
		/* x (log x - 1), scaled to keep the product in range */
		ld ml;
		ld m = fast_two_sum_l(lx, -1.0L, &ml);
		ml += ll;
		ld xs = xh * 0x1p-100L, pe;
		ld p = two_prod_l(xs, m, &pe);
		*lo = 0;
		return (p + (pe + xs * ml)) * 0x1p100L;
	}
	ld al;
	ld ah = fast_two_sum_l(xh, -0.5L, &al);
	al += xl;
	ld pl;
	ld p = mul_ddl(ah, al, lx, ll, &pl);
	ld e1, e2;
	ld s = two_sum_l(p, -xh, &e1);
	s = two_sum_l(s, LHLOG2PI_H, &e2);
	ld u = 1 / xh, v = u * u;
	ld corr = u * (LSTIR_T0 + v * (LSTIR_T1 + v * (LSTIR_T2 + v * (LSTIR_T3 + v * (LSTIR_T4 + v * (LSTIR_T5 +
	          v * LSTIR_T6))))));
	return fast_two_sum_l(s, pl + e1 + e2 - xl + LHLOG2PI_L + corr, lo);
}

static inline ld x87_sin(ld x) { __asm__("fsin" : "+t"(x)); return x; }
static inline ld x87_cos(ld x) { __asm__("fcos" : "+t"(x)); return x; }

/* |sin(pi x)| for non-integer x (x87 kernels, about an ulp) */
static ld sinpi_abs_l(ld x, int *neg)
{
	ld n = roundl(x);
	ld r = x - n;
	ld half = n * 0.5L;
	int odd = truncl(half) != half;
	*neg = odd != (r < 0);
	r = fabsl(r);
	ld ae;
	if (r <= 0.25L) {
		ld a = two_prod_l(r, LPI2_H, &ae);
		ae += r * LPI2_L;
		return x87_sin(a) + ae * x87_cos(a);
	}
	ld b = 0.5L - r;
	ld a = two_prod_l(b, LPI2_H, &ae);
	ae += b * LPI2_L;
	return x87_cos(a) - ae * x87_sin(a);
}

static int is_int_l(ld x) { return x == truncl(x); }

ld lgammal_r(ld x, int *sg)
{
	*sg = 1;
	if (x != x)
		return x + x;
	if (fabsl(x) == INFINITY)
		return x * x;
	if (x <= 0 && is_int_l(x))
		return math_divzero(0);
	ld lo;
	if (x > 0) {
		if (x >= 16) {
			ld hi = stirling_l(x, 0, &lo);
			return hi + lo;
		}
		if (x >= 2.5L) {
			int n = (int)(x - 1.5L);
			ld pl = 0, ph = x - 1;
			for (int k = 2; k <= n; k++)
				ph = mul_ddl(ph, pl, x - k, 0, &pl);
			ld gl;
			ld gh = lg_poly_l((x - n) - 2, 0, &gl);
			ld ll;
			ld lh = log_ddl(ph, pl, &ll);
			ld e;
			ld s = two_sum_l(gh, lh, &e);
			return s + (e + gl + ll);
		}
		if (x >= 1.5L) {
			ld hi = lg_poly_l(x - 2, 0, &lo);
			return hi + lo;
		}
		ld gl, gh, ll, lh, e;
		if (x >= 0.5L) {
			gh = lg_poly_l(x - 1, 0, &gl);
		} else {
			gh = lg_poly_l(x, 0, &gl);
			ld ml;
			ld mh = __log1p_kernel_l(x, &ml);
			gh = two_sum_l(gh, -mh, &e);
			gl += e - ml;
		}
		lh = __log_kernel_l(x, &ll);
		ld s = two_sum_l(gh, -lh, &e);
		return s + (e + gl - ll);
	}
	if (x > -24) {
		int n = (int)(2.5L - x);
		ld yl;
		ld yh = two_sum_l(x, (ld)n, &yl);
		ld gl;
		ld gh = lg_poly_l(yh - 2, yl, &gl);
		ld dl = 0, dh = 1;
		int negs = 0;
		for (int k = 0; k < n; k++) {
			ld fl;
			ld fh = two_sum_l(x, (ld)k, &fl);
			if (fh < 0) {
				negs++;
				fh = -fh;
				fl = -fl;
			}
			dh = mul_ddl(dh, dl, fh, fl, &dl);
		}
		*sg = negs & 1 ? -1 : 1;
		ld ll;
		ld lh = log_ddl(dh, dl, &ll);
		ld e;
		ld s = two_sum_l(gh, -lh, &e);
		return s + (e + gl - ll);
	}
	if (x <= -0x1p63L)
		return math_divzero(0);
	int neg;
	ld sn = sinpi_abs_l(x, &neg);
	*sg = neg ? -1 : 1;
	ld yl;
	ld yh = two_sum_l(1.0L, -x, &yl);
	ld gl;
	ld gh = stirling_l(yh, yl, &gl);
	ld ll;
	ld lh = __log_kernel_l(sn, &ll);
	ld e1, e2;
	ld s = two_sum_l(LLOGPI_H, -lh, &e1);
	s = two_sum_l(s, -gh, &e2);
	return s + (e1 + e2 + LLOGPI_L - ll - gl);
}

ld lgammal(ld x) { return lgammal_r(x, &signgam); }

ld tgammal(ld x)
{
	if (x != x)
		return x + x;
	if (fabsl(x) == INFINITY) {
		if (x > 0)
			return x;
		x = x - x;
		return x / x;
	}
	if (x == 0)
		return 1 / x;
	if (x < 0 && is_int_l(x)) {
		x = x - x;
		return x / x;
	}
	if (fabsl(x) < 0x1p-65L)
		return 1 / x;
	if (x > 1755.5L)
		return math_oflow(0);
	ld lo;
	if (x >= 16) {
		ld al;
		ld ah = stirling_l(x, 0, &al);
		int e;
		ld h = __exp_kernel_l(ah, al, &lo, &e);
		return __scale_l(h, lo, e);
	}
	if (x >= 2) {
		int n = (int)(x - 2);
		ld pl = 0, ph = 1;
		for (int k = 1; k <= n; k++)
			ph = mul_ddl(ph, pl, x - k, 0, &pl);
		ld gl;
		ld gh = g_poly_l((x - n) - 2.5L, 0, &gl);
		ld hi = mul_ddl(gh, gl, ph, pl, &lo);
		return hi + lo;
	}
	if (x > -24) {
		int n = (int)(3 - x);
		ld yl;
		ld yh = two_sum_l(x, (ld)n, &yl);
		ld gl;
		ld gh = g_poly_l(yh - 2.5L, yl, &gl);
		ld dl = 0, dh = 1;
		for (int k = 0; k < n; k++) {
			ld fl;
			ld fh = two_sum_l(x, (ld)k, &fl);
			dh = mul_ddl(dh, dl, fh, fl, &dl);
		}
		ld q = gh / dh;
		ld qe;
		ld qd = two_prod_l(q, dh, &qe);
		return q + ((((gh - qd) - qe) + gl - q * dl) / dh);
	}
	int neg;
	ld sn = sinpi_abs_l(x, &neg);
	ld yl;
	ld yh = two_sum_l(1.0L, -x, &yl);
	ld gl;
	ld gh = stirling_l(yh, yl, &gl);
	ld ll;
	ld lh = __log_kernel_l(sn, &ll);
	ld e1, e2;
	ld s = two_sum_l(LLOGPI_H, -lh, &e1);
	s = two_sum_l(s, -gh, &e2);
	ld bl = e1 + e2 + LLOGPI_L - ll - gl;
	ld bh = fast_two_sum_l(s, bl, &bl);
	if (bh < -11500)
		return math_uflow(neg);
	int e;
	ld h = __exp_kernel_l(bh, bl, &lo, &e);
	ld r = __scale_l(h, lo, e);
	return neg ? -r : r;
}
