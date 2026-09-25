/* libm: special cases (C Annex F), exception flags, rounding modes and
 * spot checks against correctly rounded values from mpmath. */
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "harness.h"

/* distance in ulps between two doubles of the same sign */
static long ulps(double a, double b)
{
	int64_t ia, ib;
	memcpy(&ia, &a, 8);
	memcpy(&ib, &b, 8);
	if ((ia < 0) != (ib < 0))
		return a == b ? 0 : 1L << 40;
	long d = (long)(ia - ib);
	return d < 0 ? -d : d;
}

static long ulpsl(long double a, long double b)
{
	if (a == b)
		return 0;
	if (signbit(a) != signbit(b) || !isfinite(a) || !isfinite(b))
		return 1L << 40;
	int e;
	frexpl(b, &e);
	long double u = ldexpl(1.0L, e - 64);
	long double d = fabsl(a - b) / u;
	return d > 1e9L ? 1L << 40 : (long)(d + 0.5L);
}

static int same(double a, double b) /* identical, including NaN and the sign of zero */
{
	if (a != a || b != b)
		return a != a && b != b;
	return a == b && signbit(a) == signbit(b);
}

static const struct { const char *name; double x, y, r; } dcases[] = {
	{ "exp", 0x1.0000000000000p-1, 0, 0x1.a61298e1e069cp+0 },
	{ "exp", -0x1.a000000000000p+1, 0, 0x1.3da368521902dp-5 },
	{ "exp", 0x1.5e40000000000p+9, 0, 0x1.8625c7d4f56c2p+1010 },
	{ "exp", -0x1.7200000000000p+9, 0, 0x0.0000000000055p-1022 },
	{ "exp", 0x1.b7cdfd9d7bdbbp-34, 0, 0x1.000000006df38p+0 },
	{ "exp2", 0x1.3333333333333p-2, 0, 0x1.3b2c47bff8329p+0 },
	{ "exp2", -0x1.0ba0000000000p+10, 0, 0x0.000000000000bp-1022 },
	{ "exp2", 0x1.fff3333333333p+9, 0, 0x1.ddb680117aa8ep+1023 },
	{ "expm1", 0x1.4f8b588e368f1p-17, 0, 0x1.4f8bc681cdfb6p-17 },
	{ "expm1", -0x1.3333333333333p-2, 0, -0x1.0966f2c7907f6p-2 },
	{ "expm1", 0x1.4000000000000p+2, 0, 0x1.26d389970338fp+7 },
	{ "log", 0x1.8000000000000p-1, 0, -0x1.269621134db92p-2 },
	{ "log", 0x1.000001ad7f29bp+0, 0, 0x1.ad7f2847b6492p-24 },
	{ "log", 0x1.7e43c8800759cp+996, 0, 0x1.5963447f87fb5p+9 },
	{ "log", 0x0.0000000000001p-1022, 0, -0x1.74385446d71c3p+9 },
	{ "log", 0x1.edd2f1a9fbe77p+6, 0, 0x1.343774f3e2362p+2 },
	{ "log2", 0x1.8000000000000p+1, 0, 0x1.95c01a39fbd68p+0 },
	{ "log2", 0x1.999999999999ap-4, 0, -0x1.a934f0979a371p+1 },
	{ "log2", 0x0.012688b70e62bp-1022, 0, -0x1.01730dabca5f6p+10 },
	{ "log10", 0x1.0000000000000p+1, 0, 0x1.34413509f79ffp-2 },
	{ "log10", 0x1.0f0cf064dd597p+73, 0, 0x1.6000000000000p+4 },
	{ "log10", 0x1.6666666666666p-1, 0, -0x1.3d3d3d21ccf04p-3 },
	{ "log1p", -0x1.0000000000000p-1, 0, -0x1.62e42fefa39efp-1 },
	{ "log1p", 0x1.12e0be826d695p-30, 0, 0x1.12e0be801f1d9p-30 },
	{ "log1p", 0x1.8000000000000p+1, 0, 0x1.62e42fefa39efp+0 },
	{ "sin", 0x1.0000000000000p-1, 0, 0x1.eaee8744b05f0p-2 },
	{ "sin", 0x1.8000000000000p+1, 0, 0x1.210386db6d55bp-3 },
	{ "sin", 0x1.0f0cf064dd592p+73, 0, -0x1.b453ab76bf397p-1 },
	{ "sin", 0x1.7e43c8800759cp+996, 0, -0x1.a2c16b010e385p-1 },
	{ "sin", -0x1.ad7f29abcaf48p-26, 0, -0x1.ad7f29abcaf47p-26 },
	{ "cos", 0x1.0000000000000p-1, 0, 0x1.c1528065b7d50p-1 },
	{ "cos", 0x1.921fb54442d18p+0, 0, 0x1.1a62633145c07p-54 },
	{ "cos", 0x1.0f0cf064dd592p+73, 0, 0x1.0be2cef01c8f4p-1 },
	{ "cos", 0x1.8000000000000p+2, 0, 0x1.eb9b7097822f5p-1 },
	{ "tan", 0x1.6666666666666p-1, 0, 0x1.af406c2fc78aep-1 },
	{ "tan", 0x1.8000000000000p+0, 0, 0x1.c33ed50b88777p+3 },
	{ "tan", 0x1.0f0cf064dd592p+73, 0, -0x1.a0f79c1b6b257p+0 },
	{ "tan", -0x1.8000000000000p+1, 0, 0x1.23ef71254b86fp-3 },
	{ "asin", 0x1.3333333333333p-2, 0, 0x1.380159e14f6ffp-2 },
	{ "asin", -0x1.ccccccccccccdp-1, 0, -0x1.1ea93705fa172p+0 },
	{ "asin", 0x1.fffeb074a771dp-1, 0, 0x1.90fa9f3695aa5p+0 },
	{ "acos", 0x1.3333333333333p-2, 0, 0x1.441f5ecbeef59p+0 },
	{ "acos", -0x1.ccccccccccccdp-1, 0, 0x1.586476251e745p+1 },
	{ "acos", 0x1.fffeb074a771dp-1, 0, 0x1.25160dad27316p-8 },
	{ "atan", 0x1.3333333333333p-2, 0, 0x1.2a73a661eaf06p-2 },
	{ "atan", 0x1.0000000000000p+1, 0, 0x1.1b6e192ebbe44p+0 },
	{ "atan", -0x1.2a05f20000000p+33, 0, -0x1.921fb543d4de0p+0 },
	{ "sinh", 0x1.999999999999ap-3, 0, 0x1.9c560cd35ef81p-3 },
	{ "sinh", 0x1.8000000000000p+1, 0, 0x1.40926e70949aep+3 },
	{ "sinh", -0x1.5e00000000000p+9, 0, -0x1.d945df4f8ec8ep+1008 },
	{ "cosh", 0x1.999999999999ap-3, 0, 0x1.0523184b1ee9dp+0 },
	{ "cosh", 0x1.8000000000000p+1, 0, 0x1.422a497d6185ep+3 },
	{ "cosh", 0x1.5e00000000000p+9, 0, 0x1.d945df4f8ec8ep+1008 },
	{ "tanh", 0x1.999999999999ap-3, 0, 0x1.9439830b3a590p-3 },
	{ "tanh", -0x1.8000000000000p+1, 0, -0x1.fd77d111a0b00p-1 },
	{ "tanh", 0x1.ad7f29abcaf48p-24, 0, 0x1.ad7f29abcaf2fp-24 },
	{ "asinh", 0x1.999999999999ap-3, 0, 0x1.96ead72fe8b33p-3 },
	{ "asinh", -0x1.8000000000000p+1, 0, -0x1.d185b507edc0ep+0 },
	{ "asinh", 0x1.4e718d7d7625ap+664, 0, 0x1.cd35cd6cad20fp+8 },
	{ "acosh", 0x1.00068db8bac71p+0, 0, 0x1.cf67d7ec0d4e5p-7 },
	{ "acosh", 0x1.8000000000000p+1, 0, 0x1.c34366179d427p+0 },
	{ "acosh", 0x1.4e718d7d7625ap+664, 0, 0x1.cd35cd6cad20fp+8 },
	{ "atanh", 0x1.999999999999ap-3, 0, 0x1.9f323ecbf984cp-3 },
	{ "atanh", -0x1.ccccccccccccdp-1, 0, -0x1.78e360604b32dp+0 },
	{ "atanh", 0x1.5798ee2308c3ap-27, 0, 0x1.5798ee2308c3ap-27 },
	{ "cbrt", 0x1.0000000000000p+1, 0, 0x1.428a2f98d728bp+0 },
	{ "cbrt", -0x1.56e1fc2f8f359p-997, 0, -0x1.bff2ee48e0530p-333 },
	{ "cbrt", 0x1.b800000000000p+4, 0, 0x1.825b1b6bac03bp+1 },
	{ "erf", 0x1.3333333333333p-2, 0, 0x1.50838881dea0fp-2 },
	{ "erf", 0x1.b333333333333p+0, 0, 0x1.f7b3620b8747bp-1 },
	{ "erf", -0x1.8000000000000p+1, 0, -0x1.fffd1ac4135f9p-1 },
	{ "erf", 0x1.79ca10c924223p-67, 0, 0x1.aa4a230244ae0p-67 },
	{ "erfc", 0x1.3333333333333p-2, 0, 0x1.57be3bbf10af8p-1 },
	{ "erfc", 0x1.b333333333333p+0, 0, 0x1.0993be8f17094p-6 },
	{ "erfc", 0x1.4000000000000p+3, 0, 0x1.7d8a7f2a8a2d0p-149 },
	{ "erfc", 0x1.a000000000000p+4, 0, 0x1.284bfe1cdea24p-981 },
	{ "erfc", -0x1.0000000000000p+1, 0, 0x1.fecd70a13caf2p+0 },
	{ "tgamma", 0x1.0000000000000p-1, 0, 0x1.c5bf891b4ef6bp+0 },
	{ "tgamma", 0x1.2000000000000p+2, 0, 0x1.74371e7866c65p+3 },
	{ "tgamma", 0x1.5500000000000p+7, 0, 0x1.9589f849167a8p+1015 },
	{ "tgamma", -0x1.4000000000000p+1, 0, -0x1.e3ff812e32183p-1 },
	{ "tgamma", -0x1.e800000000000p+4, 0, -0x1.62dec6b36fe62p-109 },
	{ "tgamma", 0x1.b7cdfd9d7bdbbp-34, 0, 0x1.2a05f1ffb61ddp+33 },
	{ "tgamma", 0x1.8000000000000p+1, 0, 0x1.0000000000000p+1 },
	{ "lgamma", 0x1.0000000000000p-1, 0, 0x1.250d048e7a1bdp-1 },
	{ "lgamma", 0x1.8000000000000p+0, 0, -0x1.eeb95b094c191p-4 },
	{ "lgamma", 0x1.4000000000000p+1, 0, 0x1.2383e809a67e8p-2 },
	{ "lgamma", 0x1.9000000000000p+6, 0, 0x1.67225b4879462p+8 },
	{ "lgamma", -0x1.4000000000000p+1, 0, -0x1.ccbf9f5ed0f16p-5 },
	{ "lgamma", -0x1.e800000000000p+4, 0, -0x1.2ce7e738fffadp+6 },
	{ "lgamma", 0x1.56e1fc2f8f359p-997, 0, 0x1.5963447f87fb5p+9 },
	{ "lgamma", 0x1.000000d6bf94dp+1, 0, 0x1.6b2b43f393939p-25 },
	{ "pow", 0x1.0000000000000p+1, 0x1.0000000000000p-1, 0x1.6a09e667f3bcdp+0 },
	{ "pow", 0x1.8000000000000p+0, 0x1.9200000000000p+6, 0x1.ba4104f641d8cp+58 },
	{ "pow", 0x1.ff7ced916872bp-1, -0x1.86a0000000000p+16, 0x1.4469adc1762d6p+144 },
	{ "pow", 0x1.4000000000000p+3, -0x1.2c00000000000p+8, 0x1.56e1fc2f8f359p-997 },
	{ "pow", -0x1.0000000000000p+1, 0x1.8000000000000p+1, -0x1.0000000000000p+3 },
	{ "pow", 0x1.0000000000000p+1, -0x1.0c80000000000p+10, 0x0.0000000000001p-1022 },
	{ "atan2", 0x1.0000000000000p+0, -0x1.0000000000000p+0, 0x1.2d97c7f3321d2p+1 },
	{ "atan2", -0x1.0000000000000p+1, 0x1.8000000000000p+1, -0x1.2d0ead6066395p-1 },
	{ "atan2", 0x1.56e1fc2f8f359p-997, -0x1.0000000000000p+0, 0x1.921fb54442d18p+1 },
	{ "hypot", 0x1.8000000000000p+1, 0x1.0000000000000p+2, 0x1.4000000000000p+2 },
	{ "hypot", 0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.0e4d50f99b211p+997 },
	{ "hypot", 0x0.012688b70e62bp-1022, 0x0.03739a252b281p-1022, 0x0.03a365ff2ea11p-1022 },
};
static const struct { const char *name; long double x, r; } lcases[] = {
	{ "expl", 0x8000000000000000p-64L, 0xd3094c70f034de4cp-63L },
	{ "expl", -0xabe1000000000000p-50L, 0xfeea6bcac562283bp-15934L },
	{ "expl", 0xa7c5ac471b478423p-80L, 0x800053e2f1a0737fp-63L },
	{ "logl", 0xc000000000000000p-62L, 0x8c9f53d5681854bbp-63L },
	{ "logl", 0x9c3d73864f3805c0p-13351L, -0x8fe95c8a78a8b80ep-50L },
	{ "logl", 0xffffef39085f4a12p-64L, -0x8637c16b96017c77p-83L },
	{ "log2l", 0xa000000000000000p-60L, 0xd49a784bcd1b8afep-62L },
	{ "sinl", 0x8000000000000000p-64L, 0xf57743a2582f7f44p-65L },
	{ "sinl", 0x878678326eac9000p10L, -0xda29d5bb5f9cb87dp-64L },
	{ "sinl", 0xd1ba8323fe558c61p13224L, 0x90de0837a745738ap-64L },
	{ "cosl", 0x8000000000000000p-62L, -0xd51132ba9b902522p-65L },
	{ "tanl", 0xc000000000000000p-63L, 0xe19f6a85c43bbad3p-60L },
	{ "atanl", 0xc000000000000000p-64L, 0xa4bc7d1934f70924p-64L },
	{ "sinhl", 0xcccccccccccccccdp-67L, 0xcd24399bdacd96b9p-67L },
	{ "sinhl", 0xa000000000000000p-59L, 0xe758445b47401fcap-36L },
	{ "tanhl", 0xa3d70a3d70a3d70ap-70L, 0xa3d5a45722caf014p-70L },
	{ "acoshl", 0xc000000000000000p-63L, 0xf661657628b04ca6p-64L },
	{ "cbrtl", 0xa000000000000000p-60L, 0x89e24209c3827654p-62L },
	{ "expm1l", 0xdbe6fecebdedd5bfp-97L, 0xdbe6feceed2717d8p-97L },
	{ "log1pl", -0x8000000000000000p-65L, -0x934b1089a6dc93c2p-65L },
};

static double call1(const char *n, double x, double y)
{
	static const struct { const char *n; double (*f)(double); } t1[] = {
		{ "exp", exp }, { "exp2", exp2 }, { "expm1", expm1 }, { "log", log }, { "log2", log2 }, { "log10", log10 },
		{ "log1p", log1p }, { "sin", sin }, { "cos", cos }, { "tan", tan }, { "asin", asin }, { "acos", acos },
		{ "atan", atan }, { "sinh", sinh }, { "cosh", cosh }, { "tanh", tanh }, { "asinh", asinh }, { "acosh", acosh },
		{ "atanh", atanh }, { "cbrt", cbrt }, { "erf", erf }, { "erfc", erfc }, { "tgamma", tgamma }, { "lgamma", lgamma },
	};
	static const struct { const char *n; double (*f)(double, double); } t2[] = {
		{ "pow", pow }, { "atan2", atan2 }, { "hypot", hypot },
	};
	for (unsigned i = 0; i < sizeof t1 / sizeof t1[0]; i++)
		if (!strcmp(n, t1[i].n))
			return t1[i].f(x);
	for (unsigned i = 0; i < sizeof t2 / sizeof t2[0]; i++)
		if (!strcmp(n, t2[i].n))
			return t2[i].f(x, y);
	return NAN;
}

static long double calll(const char *n, long double x)
{
	static const struct { const char *n; long double (*f)(long double); } t[] = {
		{ "expl", expl }, { "logl", logl }, { "log2l", log2l }, { "sinl", sinl }, { "cosl", cosl }, { "tanl", tanl },
		{ "atanl", atanl }, { "sinhl", sinhl }, { "tanhl", tanhl }, { "acoshl", acoshl }, { "cbrtl", cbrtl },
		{ "expm1l", expm1l }, { "log1pl", log1pl },
	};
	for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
		if (!strcmp(n, t[i].n))
			return t[i].f(x);
	return NAN;
}

static void spot_checks(void)
{
	for (unsigned i = 0; i < sizeof dcases / sizeof dcases[0]; i++) {
		double r = call1(dcases[i].name, dcases[i].x, dcases[i].y);
		if (ulps(r, dcases[i].r) > 1) {
			t_puts(dcases[i].name);
			t_puts(": ");
			t_putl(i);
			t_puts("\n");
			CHECK(!"double spot check within 1 ulp");
		}
	}
	for (unsigned i = 0; i < sizeof lcases / sizeof lcases[0]; i++) {
		long double r = calll(lcases[i].name, lcases[i].x);
		if (ulpsl(r, lcases[i].r) > 2) {
			t_puts(lcases[i].name);
			t_puts(": ");
			t_putl(i);
			t_puts("\n");
			CHECK(!"long double spot check within 2 ulp");
		}
	}
	/* float versions round the double result */
	CHECK(expf(1.0f) == 0x1.5bf0a8p+1f);
	CHECK(sinf(1e10f) == (float)sin(1e10));
	CHECK(powf(2.0f, 10.0f) == 1024.0f);
}

/* f() raises exactly the exceptions in want (among the five) */
#define FLAGS(expr, want) do { \
	feclearexcept(FE_ALL_EXCEPT); \
	volatile double v_ = (expr); (void)v_; \
	CHECK(fetestexcept(FE_ALL_EXCEPT) == (want)); \
} while (0)

static void special_cases(void)
{
	double inf = INFINITY, qnan = NAN;
	/* pow (Annex F.10.4.4) */
	CHECK(pow(qnan, 0.0) == 1 && pow(qnan, -0.0) == 1);
	CHECK(pow(1.0, qnan) == 1 && pow(-1.0, inf) == 1);
	CHECK(same(pow(-0.0, -3.0), -inf));
	FLAGS(pow(-0.0, -3.0), FE_DIVBYZERO);
	CHECK(same(pow(-0.0, 3.0), -0.0) && same(pow(-0.0, 4.0), 0.0));
	CHECK(same(pow(-inf, 3.0), -inf) && same(pow(-inf, -3.0), -0.0) && pow(-inf, 2.0) == inf);
	CHECK(isnan(pow(-8.0, 1.0 / 3)));
	FLAGS(pow(-8.0, 1.0 / 3), FE_INVALID);
	CHECK(pow(0.5, inf) == 0 && pow(0.5, -inf) == inf && pow(2.0, -inf) == 0);
	CHECK(pow(-2.0, 3.0) == -8 && pow(2.0, 0.5) == sqrt(2.0));
	FLAGS(pow(10.0, 400.0), FE_OVERFLOW | FE_INEXACT);
	/* exp/log */
	FLAGS(exp(-inf), 0);
	CHECK(exp(-inf) == 0 && exp(inf) == inf && exp(0.0) == 1);
	FLAGS(exp(1000.0), FE_OVERFLOW | FE_INEXACT);
	FLAGS(exp(-1000.0), FE_UNDERFLOW | FE_INEXACT);
	CHECK(same(log(0.0), -inf) && same(log(-0.0), -inf));
	FLAGS(log(0.0), FE_DIVBYZERO);
	FLAGS(log(-1.0), FE_INVALID);
	CHECK(log(1.0) == 0 && !signbit(log(1.0)) && log2(1024.0) == 10 && log2(0x1p-1074) == -1074);
	CHECK(same(log1p(-0.0), -0.0) && log1p(-1.0) == -inf && same(expm1(-0.0), -0.0) && expm1(-inf) == -1);
	/* trig and inverse */
	CHECK(same(sin(-0.0), -0.0) && same(tan(-0.0), -0.0) && cos(-0.0) == 1);
	FLAGS(sin(inf), FE_INVALID);
	CHECK(same(atan2(0.0, -0.0), M_PI) && same(atan2(-0.0, -0.0), -M_PI) && same(atan2(-0.0, 0.0), -0.0));
	CHECK(atan2(inf, -inf) == 3 * M_PI_4 && atan2(-inf, inf) == -M_PI_4 && atan2(1.0, 0.0) == M_PI_2);
	CHECK(same(asin(-0.0), -0.0) && acos(1.0) == 0 && same(atan(-inf), -M_PI_2));
	FLAGS(asin(2.0), FE_INVALID);
	/* hyperbolic */
	CHECK(same(sinh(-0.0), -0.0) && cosh(-inf) == inf && tanh(-inf) == -1 && same(atanh(-0.0), -0.0));
	CHECK(atanh(1.0) == inf && isnan(acosh(0.5)) && same(asinh(-inf), -inf));
	FLAGS(atanh(-1.0), FE_DIVBYZERO);
	/* others */
	CHECK(hypot(inf, qnan) == inf && hypot(qnan, -inf) == inf && isnan(hypot(qnan, 1.0)));
	CHECK(same(cbrt(-0.0), -0.0) && cbrt(-27.0) == -3 && cbrt(0x1p-1074) == 0x1p-358);
	CHECK(same(sqrt(-0.0), -0.0) && isnan(sqrt(-1.0)));
	CHECK(same(fmod(-5.0, 3.0), -2.0) && same(fmod(-6.0, 3.0), -0.0) && fmod(1.0, inf) == 1);
	CHECK(remainder(5.0, 2.0) == 1 && remainder(7.0, 2.0) == -1 && same(remainder(-4.0, 2.0), -0.0));
	int q;
	CHECK(remquo(10.0, 3.0, &q) == 1 && (q & 7) == 3);
	CHECK(remquo(-11.0, 3.0, &q) == 1 && -q % 8 == 4);
	CHECK(nexttoward(1.0, 1.0L + 0x1p-60L) == 1.0 + 0x1p-52);
	CHECK(nextafter(0.0, -1.0) == -0x1p-1074 && nextafter(0x1p-1074, 0) == 0);
	/* gamma */
	CHECK(tgamma(5.0) == 24 && ulps(tgamma(0.5), 0x1.c5bf891b4ef6bp+0) <= 1 && same(tgamma(-0.0), -inf) && isnan(tgamma(-2.0)));
	FLAGS(tgamma(-2.0), FE_INVALID);
	CHECK(lgamma(1.0) == 0 && lgamma(2.0) == 0 && lgamma(-2.0) == inf);
	(void)lgamma(-0.5);
	CHECK(signgam == -1);
	(void)lgamma(-1.5);
	CHECK(signgam == 1);
	int sg;
	(void)lgamma_r(-2.5, &sg);
	CHECK(sg == -1);
	CHECK(erf(inf) == 1 && erf(-inf) == -1 && erfc(-inf) == 2 && erfc(inf) == 0 && same(erf(-0.0), -0.0));
	/* decomposition */
	int e;
	CHECK(frexp(0x1p-1074, &e) == 0.5 && e == -1073);
	CHECK(ldexp(0.75, -1074) == 0x1p-1074 && scalbn(1.0, 2000) == inf && scalbln(1.0, -5000L) == 0);
	CHECK(ilogb(0.0) == FP_ILOGB0 && ilogb(0x1p-1074) == -1074 && logb(-inf) == inf && logb(0.0) == -inf);
	double ip;
	CHECK(same(modf(-3.5, &ip), -0.5) && ip == -3 && same(modf(-inf, &ip), -0.0));
	CHECK(copysign(3.0, -0.0) == -3 && fabs(-0.0) == 0 && !signbit(fabs(-0.0)));
	CHECK(fmax(qnan, 1.0) == 1 && fmin(1.0, qnan) == 1 && fdim(1.0, 3.0) == 0);
	CHECK(isnan(nan("")) && isnan(nanf("")) && isnan(nanl("")));
}

static void rounding(void)
{
	CHECK(round(-2.5) == -3 && round(0.49999999999999994) == 0 && same(round(-0.4), -0.0));
	CHECK(trunc(-1.5) == -1 && floor(-0.5) == -1 && same(ceil(-0.5), -0.0) && same(floor(-0.0), -0.0));
	CHECK(rint(2.5) == 2 && rint(3.5) == 4 && lrint(-2.5) == -2 && llrint(2.5) == 2 && lround(2.5) == 3);
	CHECK(lroundl(-2.5L) == -3 && llrintl(7.5L) == 8 && roundl(0.5L) == 1 && floorl(-0.25L) == -1);
	CHECK(roundf(1.5f) == 2 && lrintf(1.5f) == 2 && truncf(-2.75f) == -2);

	CHECK(fegetround() == FE_TONEAREST);
	CHECK(fesetround(FE_UPWARD) == 0 && fegetround() == FE_UPWARD);
	CHECK(rint(0.25) == 1 && rintl(0.25L) == 1 && lrint(0.25) == 1);
	volatile double third = 1.0;
	third /= 3;
	CHECK(third > 0.333333333333333314829616256247 );
	fesetround(FE_DOWNWARD);
	CHECK(rint(-0.25) == -1 && nearbyint(0.75) == 0);
	CHECK(same(fma(1.0, 1.0, -1.0), -0.0)); /* exact zero sum is -0 when rounding down */
	fesetround(FE_TOWARDZERO);
	CHECK(rint(-1.75) == -1 && exp(1000.0) == 0x1.fffffffffffffp+1023);
	fesetround(FE_TONEAREST);
	CHECK(fesetround(12345) != 0);

	/* nearbyint never raises inexact, and keeps flags already set */
	feclearexcept(FE_ALL_EXCEPT);
	(void)nearbyint(2.5);
	(void)nearbyintl(2.5L);
	CHECK(!fetestexcept(FE_INEXACT));
	feraiseexcept(FE_INEXACT);
	(void)nearbyintl(3.25L);
	CHECK(fetestexcept(FE_INEXACT));

	/* environment save/restore */
	fenv_t env;
	feclearexcept(FE_ALL_EXCEPT);
	feraiseexcept(FE_OVERFLOW);
	CHECK(feholdexcept(&env) == 0);
	CHECK(!fetestexcept(FE_ALL_EXCEPT));
	fesetround(FE_UPWARD);
	feraiseexcept(FE_DIVBYZERO);
	CHECK(feupdateenv(&env) == 0);
	CHECK(fegetround() == FE_TONEAREST);
	CHECK(fetestexcept(FE_ALL_EXCEPT) == (FE_OVERFLOW | FE_DIVBYZERO));
	fexcept_t fl;
	fegetexceptflag(&fl, FE_ALL_EXCEPT);
	feclearexcept(FE_ALL_EXCEPT);
	fesetexceptflag(&fl, FE_OVERFLOW);
	CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_OVERFLOW);
	fesetenv(FE_DFL_ENV);
	CHECK(!fetestexcept(FE_ALL_EXCEPT) && fegetround() == FE_TONEAREST);
}

static void fused(void)
{
	CHECK(fma(1 + 0x1p-52, 1 - 0x1p-52, -1.0) == -0x1p-104);
	CHECK(fma(0x1p-1022, 0x1p-52, 0x1p-1074) == 0x1p-1073);
	CHECK(fma(0x1.fffffffffffffp1023, 2.0, -0x1.fffffffffffffp1023) == 0x1.fffffffffffffp1023);
	CHECK(isnan(fma(INFINITY, 0.0, 1.0)) && fma(2.0, 3.0, INFINITY) == INFINITY);
	CHECK(fmaf(1 + 0x1p-23f, 1 - 0x1p-23f, -1.0f) == -0x1p-46f);
	CHECK(fmal(1 + 0x1p-63L, 1 - 0x1p-63L, -1.0L) == -0x1p-126L);
	/* round to nearest even of an exact tie */
	CHECK(fma(0x1p52 + 1, 1.0, 0.5) == 0x1p52 + 2);
}

int main(void)
{
	spot_checks();
	special_cases();
	rounding();
	fused();
	return t_done();
}
