/* <complex.h> and complex <tgmath.h> */
#include <complex.h>
#include <float.h>
#include <math.h>
#include "harness.h"

#define S(x) _Generic((x), float: 1, double: 2, long double: 3, float _Complex: 4, double _Complex: 5, long double _Complex: 6)

static int same(double a, double b)
{
	if (isnan(a) || isnan(b))
		return isnan(a) && isnan(b);
	return a == b && signbit(a) == signbit(b);
}

/* w must be exactly (re, im); signs of zeros included */
#define EXACT(w, re, im) CHECK(same(creal(w), re) && same(cimag(w), im))
/* within n ulps of the expected value (componentwise) */
static int near(double got, double want, double n)
{
	if (isinf(want))
		return got == want;
	if (want == 0)
		return fabs(got) < 1e-300;
	return fabs(got - want) <= n * fabs(want) * DBL_EPSILON;
}
#define NEAR(w, re, im) CHECK(near(creal(w), re, 1) && near(cimag(w), im, 1))

static const double inf = INFINITY, nan_ = NAN, pi = 3.14159265358979323846, pi2 = 1.57079632679489661923;

static void annex_g(void)
{
	/* cexp */
	EXACT(cexp(CMPLX(0.0, 0.0)), 1.0, 0.0);
	EXACT(cexp(CMPLX(-0.0, -0.0)), 1.0, -0.0);
	EXACT(cexp(CMPLX(inf, 0.0)), inf, 0.0);
	EXACT(cexp(CMPLX(-inf, 2.0)), cos(2.0) * 0.0, sin(2.0) * 0.0);
	EXACT(cexp(CMPLX(-inf, inf)), 0.0, 0.0);
	CHECK(isinf(creal(cexp(CMPLX(inf, nan_)))) && isnan(cimag(cexp(CMPLX(inf, nan_)))));
	EXACT(cexp(CMPLX(nan_, 0.0)), nan_, 0.0);
	EXACT(cexp(CMPLX(1.0, inf)), nan_, nan_);
	/* clog */
	EXACT(clog(CMPLX(-0.0, 0.0)), -inf, pi);
	EXACT(clog(CMPLX(0.0, -0.0)), -inf, -0.0);
	EXACT(clog(CMPLX(1.0, 0.0)), 0.0, 0.0);
	EXACT(clog(CMPLX(-inf, 1.0)), inf, pi);
	EXACT(clog(CMPLX(inf, -inf)), inf, -pi / 4);
	EXACT(clog(CMPLX(nan_, inf)), inf, nan_);
	EXACT(clog(CMPLX(nan_, 1.0)), nan_, nan_);
	/* csqrt */
	EXACT(csqrt(CMPLX(0.0, -0.0)), 0.0, -0.0);
	EXACT(csqrt(CMPLX(-0.0, 0.0)), 0.0, 0.0);
	EXACT(csqrt(CMPLX(nan_, inf)), inf, inf);
	EXACT(csqrt(CMPLX(-inf, 2.0)), 0.0, inf);
	EXACT(csqrt(CMPLX(inf, -2.0)), inf, -0.0);
	EXACT(csqrt(CMPLX(-4.0, 0.0)), 0.0, 2.0);
	EXACT(csqrt(CMPLX(-4.0, -0.0)), 0.0, -2.0);
	EXACT(csqrt(CMPLX(4.0, 0.0)), 2.0, 0.0);
	/* ccosh / csinh / ctanh */
	EXACT(ccosh(CMPLX(0.0, 0.0)), 1.0, 0.0);
	EXACT(ccosh(CMPLX(inf, 0.0)), inf, 0.0);
	EXACT(csinh(CMPLX(0.0, 0.0)), 0.0, 0.0);
	EXACT(csinh(CMPLX(inf, 0.0)), inf, 0.0);
	EXACT(csinh(CMPLX(-0.0, -0.0)), -0.0, -0.0);
	EXACT(ctanh(CMPLX(0.0, 0.0)), 0.0, 0.0);
	EXACT(ctanh(CMPLX(inf, 1.0)), 1.0, 0.0);
	EXACT(ctanh(CMPLX(-inf, -1.0)), -1.0, -0.0);
	EXACT(ctanh(CMPLX(nan_, 0.0)), nan_, 0.0);
	/* inverse functions */
	EXACT(cacos(CMPLX(0.0, 0.0)), pi2, -0.0);
	EXACT(cacos(CMPLX(-inf, 1.0)), pi, -inf);
	EXACT(cacos(CMPLX(inf, 1.0)), 0.0, -inf);
	EXACT(cacos(CMPLX(1.0, 0.0)), 0.0, -0.0);
	EXACT(cacosh(CMPLX(0.0, 0.0)), 0.0, pi2);
	EXACT(cacosh(CMPLX(-0.0, -0.0)), 0.0, -pi2);
	EXACT(cacosh(CMPLX(inf, inf)), inf, pi / 4);
	EXACT(casinh(CMPLX(0.0, 0.0)), 0.0, 0.0);
	EXACT(casinh(CMPLX(inf, 1.0)), inf, 0.0);
	EXACT(casinh(CMPLX(nan_, 0.0)), nan_, 0.0);
	EXACT(catanh(CMPLX(0.0, 0.0)), 0.0, 0.0);
	EXACT(catanh(CMPLX(1.0, 0.0)), inf, 0.0);
	EXACT(catanh(CMPLX(2.0, inf)), 0.0, pi2);
	EXACT(catanh(CMPLX(1e308, inf)), 0.0, pi2);
	EXACT(catanh(CMPLX(inf, nan_)), 0.0, nan_);
	EXACT(catan(CMPLX(0.0, 1.0)), 0.0, inf);
	/* cproj */
	EXACT(cproj(CMPLX(inf, -3.0)), inf, -0.0);
	EXACT(cproj(CMPLX(nan_, -inf)), inf, -0.0);
	EXACT(cproj(CMPLX(1.0, 2.0)), 1.0, 2.0);
}

static void values(void)
{
	/* from mpmath, rounded to double */
	NEAR(cexp(CMPLX(1.0, 1.0)), 1.4686939399158851, 2.2873552871788423);
	NEAR(clog(CMPLX(3.0, -4.0)), 1.6094379124341003, -0.9272952180016122);
	NEAR(csqrt(CMPLX(0.0, 1.0)), 0.7071067811865476, 0.7071067811865476);
	NEAR(csin(CMPLX(1.0, 2.0)), 3.165778513216168, 1.9596010414216058);
	NEAR(ccos(CMPLX(1.0, 2.0)), 2.0327230070196656, -3.0518977991518);
	NEAR(ctan(CMPLX(1.0, 2.0)), 0.03381282607989669, 1.0147936161466335);
	NEAR(casin(CMPLX(1.0, 2.0)), 0.42707858639247614, 1.5285709194809982);
	NEAR(cacos(CMPLX(1.0, 2.0)), 1.1437177404024206, -1.5285709194809982);
	NEAR(catan(CMPLX(1.0, 2.0)), 1.3389725222944935, 0.40235947810852507);
	NEAR(catanh(CMPLX(0.6, 0.8)), 0.34657359027997264, 0.7853981633974483);
	/* i^2 = exp(2 log i) = exp(i pi); evaluated in long double, the
	 * imaginary part is sin of the long double pi, not of the double */
	double complex sq = cpow(CMPLX(0.0, 1.0), CMPLX(2.0, 0.0));
	CHECK(creal(sq) == -1 && fabs(cimag(sq)) < 1e-18);
	/* near the unit circle clog must not cancel: log|0.6+0.8i + tiny| */
	double complex z = CMPLX(0.6, 0.8);
	double r = creal(clog(z));
	/* 0.6^2 + 0.8^2 in binary is 1 - 2^-54 or so; the result is tiny, not 0 */
	CHECK(r != 0 && fabs(r) < 1e-15);
	/* results beyond the range of exp: cosh(750) cos(1) is finite in no
	 * double, but cexp(710 + i pi/2) has a finite imaginary part */
	CHECK(isfinite(cimag(cexp(CMPLX(709.9, 1e-10)))));
	/* exp(1000) sin(2^-1074) is finite though exp(1000) is not */
	NEAR(cexp(CMPLX(1000.0, 0x1p-1074)), inf, 9.733444573000164e+110);
	CHECK(near(cabs(CMPLX(3e300, 4e300)), 5e300, 1));
	CHECK(near(carg(CMPLX(-1.0, 0.0)), pi, 1));
	/* tiny arguments stay accurate */
	NEAR(csqrt(CMPLX(0.0, 0x1p-1074)), 1.5717277847026288e-162, 1.5717277847026288e-162);
	NEAR(csqrt(CMPLX(1e308, 1.0)), 1e154, 5e-155);
}

static void other_types(void)
{
	float complex f = csqrtf(CMPLXF(-4.0f, 0.0f));
	CHECK(crealf(f) == 0 && cimagf(f) == 2);
	long double complex l = cexpl(CMPLXL(0.0L, 3.14159265358979323846264338327950288L));
	CHECK(fabsl(creall(l) + 1) < 1e-18L && fabsl(cimagl(l)) < 1e-18L);
	l = clogl(CMPLXL(0.0L, 1.0L));
	CHECK(creall(l) == 0 && fabsl(cimagl(l) - 1.57079632679489661923132169163975144L) < 1e-18L);
	CHECK(cabsf(CMPLXF(3, 4)) == 5 && cabsl(CMPLXL(3, 4)) == 5);
	double complex c = 1.0 + 2.0 * I;
	CHECK(creal(c) == 1 && cimag(c) == 2 && creal(conj(c)) == 1 && cimag(conj(c)) == -2);
}

/* <tgmath.h> dispatches on the complex types */
#include <tgmath.h>
static void generic(void)
{
	float complex fz = 1.0f;
	double complex dz = 1.0;
	long double complex lz = 1.0L;
	CHECK(S(sqrt(fz)) == 4 && S(sqrt(dz)) == 5 && S(sqrt(lz)) == 6);
	CHECK(S(sqrt(2.0f)) == 1 && S(sqrt(2)) == 2 && S(sqrt(2.0L)) == 3);
	CHECK(S(exp(fz)) == 4 && S(sin(lz)) == 6 && S(atanh(dz)) == 5);
	CHECK(S(fabs(fz)) == 1 && S(fabs(lz)) == 3);
	CHECK(S(pow(2.0f, fz)) == 4 && S(pow(2.0, fz)) == 5 && S(pow(lz, 2)) == 6 && S(pow(2.0f, 3.0f)) == 1);
	CHECK(S(carg(2.0f)) == 1 && S(creal(lz)) == 3 && S(conj(1.0)) == 5 && S(cproj(fz)) == 4);
	CHECK(creal(sqrt(-4.0 + 0.0 * I)) == 0 && cimag(sqrt(-4.0 + 0.0 * I)) == 2);
}

int main(void)
{
	annex_g();
	values();
	other_types();
	generic();
	return t_done();
}
