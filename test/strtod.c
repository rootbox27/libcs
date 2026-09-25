/* Number parsing. The strtod table was generated from glibc (correctly
 * rounded) and compares exact bit patterns, end offsets and ERANGE. */
#include "harness.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct fcase {
	const char *s;
	uint64_t d; int dend, derr;
	uint32_t f; int fend, ferr;
	uint64_t lm; uint16_t le; int lend, lerr;
};
#define S(...) { __VA_ARGS__ }
static const struct fcase fcases[] = {
	S("0", 0x0000000000000000ULL, 1, 0, 0x00000000U, 1, 0, 0x0000000000000000ULL, 0x0000, 1, 0),
	S("-0", 0x8000000000000000ULL, 2, 0, 0x80000000U, 2, 0, 0x0000000000000000ULL, 0x8000, 2, 0),
	S("1", 0x3ff0000000000000ULL, 1, 0, 0x3f800000U, 1, 0, 0x8000000000000000ULL, 0x3fff, 1, 0),
	S("  +1.5", 0x3ff8000000000000ULL, 6, 0, 0x3fc00000U, 6, 0, 0xc000000000000000ULL, 0x3fff, 6, 0),
	S("0.1", 0x3fb999999999999aULL, 3, 0, 0x3dcccccdU, 3, 0, 0xcccccccccccccccdULL, 0x3ffb, 3, 0),
	S("3.14159265358979323846", 0x400921fb54442d18ULL, 22, 0, 0x40490fdbU, 22, 0, 0xc90fdaa22168c235ULL, 0x4000, 22, 0),
	S("1e10", 0x4202a05f20000000ULL, 4, 0, 0x501502f9U, 4, 0, 0x9502f90000000000ULL, 0x4020, 4, 0),
	S("1E-10", 0x3ddb7cdfd9d7bdbbULL, 5, 0, 0x2edbe6ffU, 5, 0, 0xdbe6fecebdedd5bfULL, 0x3fdd, 5, 0),
	S("123.456e-2", 0x3ff3c0c1fc8f3238ULL, 10, 0, 0x3f9e0610U, 10, 0, 0x9e060fe47991bc56ULL, 0x3fff, 10, 0),
	S(".5", 0x3fe0000000000000ULL, 2, 0, 0x3f000000U, 2, 0, 0x8000000000000000ULL, 0x3ffe, 2, 0),
	S("5.", 0x4014000000000000ULL, 2, 0, 0x40a00000U, 2, 0, 0xa000000000000000ULL, 0x4001, 2, 0),
	S("-.25e+1", 0xc004000000000000ULL, 7, 0, 0xc0200000U, 7, 0, 0xa000000000000000ULL, 0xc000, 7, 0),
	S("1e", 0x3ff0000000000000ULL, 1, 0, 0x3f800000U, 1, 0, 0x8000000000000000ULL, 0x3fff, 1, 0),
	S("1e+", 0x3ff0000000000000ULL, 1, 0, 0x3f800000U, 1, 0, 0x8000000000000000ULL, 0x3fff, 1, 0),
	S("1.5x", 0x3ff8000000000000ULL, 3, 0, 0x3fc00000U, 3, 0, 0xc000000000000000ULL, 0x3fff, 3, 0),
	S("abc", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S("", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S(" ", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S(".", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S("-", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S("+.e1", 0x0000000000000000ULL, 0, 0, 0x00000000U, 0, 0, 0x0000000000000000ULL, 0x0000, 0, 0),
	S("0x", 0x0000000000000000ULL, 1, 0, 0x00000000U, 1, 0, 0x0000000000000000ULL, 0x0000, 1, 0),
	S("0x1", 0x3ff0000000000000ULL, 3, 0, 0x3f800000U, 3, 0, 0x8000000000000000ULL, 0x3fff, 3, 0),
	S("0x1p-2", 0x3fd0000000000000ULL, 6, 0, 0x3e800000U, 6, 0, 0x8000000000000000ULL, 0x3ffd, 6, 0),
	S("0X1.8P1", 0x4008000000000000ULL, 7, 0, 0x40400000U, 7, 0, 0xc000000000000000ULL, 0x4000, 7, 0),
	S("0x.8", 0x3fe0000000000000ULL, 4, 0, 0x3f000000U, 4, 0, 0x8000000000000000ULL, 0x3ffe, 4, 0),
	S("0xg", 0x0000000000000000ULL, 1, 0, 0x00000000U, 1, 0, 0x0000000000000000ULL, 0x0000, 1, 0),
	S("0x1.fffffffffffff8p0", 0x4000000000000000ULL, 20, 0, 0x40000000U, 20, 0, 0xfffffffffffffc00ULL, 0x3fff, 20, 0),
	S("inf", 0x7ff0000000000000ULL, 3, 0, 0x7f800000U, 3, 0, 0x8000000000000000ULL, 0x7fff, 3, 0),
	S("-INF", 0xfff0000000000000ULL, 4, 0, 0xff800000U, 4, 0, 0x8000000000000000ULL, 0xffff, 4, 0),
	S("Infinity", 0x7ff0000000000000ULL, 8, 0, 0x7f800000U, 8, 0, 0x8000000000000000ULL, 0x7fff, 8, 0),
	S("infinit", 0x7ff0000000000000ULL, 3, 0, 0x7f800000U, 3, 0, 0x8000000000000000ULL, 0x7fff, 3, 0),
	S("nan", 0x7ff8000000000000ULL, 3, 0, 0x7fc00000U, 3, 0, 0xc000000000000000ULL, 0x7fff, 3, 0),
	S("NAN(abc_1)", 0x7ff8000000000000ULL, 10, 0, 0x7fc00000U, 10, 0, 0xc000000000000000ULL, 0x7fff, 10, 0),
	S("nan(", 0x7ff8000000000000ULL, 3, 0, 0x7fc00000U, 3, 0, 0xc000000000000000ULL, 0x7fff, 3, 0),
	S("nan()x", 0x7ff8000000000000ULL, 5, 0, 0x7fc00000U, 5, 0, 0xc000000000000000ULL, 0x7fff, 5, 0),
	S("1e308", 0x7fe1ccf385ebc8a0ULL, 5, 0, 0x7f800000U, 5, 1, 0x8e679c2f5e44ff8fULL, 0x43fe, 5, 0),
	S("1.7976931348623157e308", 0x7fefffffffffffffULL, 22, 0, 0x7f800000U, 22, 1, 0xfffffffffffff7acULL, 0x43fe, 22, 0),
	S("1.7976931348623158e308", 0x7fefffffffffffffULL, 22, 0, 0x7f800000U, 22, 1, 0xfffffffffffffbafULL, 0x43fe, 22, 0),
	S("1.7976931348623159e308", 0x7ff0000000000000ULL, 22, 1, 0x7f800000U, 22, 1, 0xffffffffffffffb1ULL, 0x43fe, 22, 0),
	S("1e309", 0x7ff0000000000000ULL, 5, 1, 0x7f800000U, 5, 1, 0xb201833b35d63f73ULL, 0x4401, 5, 0),
	S("-1e999", 0xfff0000000000000ULL, 6, 1, 0xff800000U, 6, 1, 0xc2d7c194b0fe2338ULL, 0xccf5, 6, 0),
	S("2.2250738585072014e-308", 0x0010000000000000ULL, 23, 0, 0x00000000U, 23, 1, 0x8000000000000046ULL, 0x3c01, 23, 0),
	S("2.2250738585072011e-308", 0x000fffffffffffffULL, 23, 1, 0x00000000U, 23, 1, 0xfffffffffffff6d5ULL, 0x3c00, 23, 0),
	S("4.9406564584124654e-324", 0x0000000000000001ULL, 23, 1, 0x00000000U, 23, 1, 0xffffffffffffff64ULL, 0x3bcc, 23, 0),
	S("2.4703282292062327e-324", 0x0000000000000000ULL, 23, 1, 0x00000000U, 23, 1, 0xffffffffffffff64ULL, 0x3bcb, 23, 0),
	S("2.4703282292062328e-324", 0x0000000000000001ULL, 23, 1, 0x00000000U, 23, 1, 0x8000000000000127ULL, 0x3bcc, 23, 0),
	S("1e-324", 0x0000000000000000ULL, 6, 1, 0x00000000U, 6, 1, 0xcf42894a5dce35eaULL, 0x3bca, 6, 0),
	S("1e-400", 0x0000000000000000ULL, 6, 1, 0x00000000U, 6, 1, 0x95fe7e07c91efafaULL, 0x3ace, 6, 0),
	S("0e-99999", 0x0000000000000000ULL, 8, 0, 0x00000000U, 8, 0, 0x0000000000000000ULL, 0x0000, 8, 0),
	S("0e99999", 0x0000000000000000ULL, 7, 0, 0x00000000U, 7, 0, 0x0000000000000000ULL, 0x0000, 7, 0),
	S("1e-99999", 0x0000000000000000ULL, 8, 1, 0x00000000U, 8, 1, 0x0000000000000000ULL, 0x0000, 8, 1),
	S("1e99999", 0x7ff0000000000000ULL, 7, 1, 0x7f800000U, 7, 1, 0x8000000000000000ULL, 0x7fff, 7, 1),
	S("9007199254740993", 0x4340000000000000ULL, 16, 0, 0x5a000000U, 16, 0, 0x8000000000000400ULL, 0x4034, 16, 0),
	S("9007199254740993.0000000000000000000000000000001", 0x4340000000000001ULL, 48, 0, 0x5a000000U, 48, 0, 0x8000000000000400ULL, 0x4034, 48, 0),
	S("9007199254740995", 0x4340000000000002ULL, 16, 0, 0x5a000000U, 16, 0, 0x8000000000000c00ULL, 0x4034, 16, 0),
	S("3.4028234663852886e38", 0x47efffffe0000000ULL, 21, 0, 0x7f7fffffU, 21, 0, 0xffffff000000000aULL, 0x407e, 21, 0),
	S("3.4028235677973366e38", 0x47effffff0000000ULL, 21, 0, 0x7f7fffffU, 21, 0, 0xffffff7fffffffa7ULL, 0x407e, 21, 0),
	S("1.17549435e-38", 0x380fffffff9fdba8ULL, 14, 0, 0x00800000U, 14, 0, 0xfffffffcfedd426eULL, 0x3f80, 14, 0),
	S("1.4e-45", 0x369ff868bf4d956aULL, 7, 0, 0x00000001U, 7, 1, 0xffc345fa6cab4c58ULL, 0x3f69, 7, 0),
	S("7.0064923216240854e-46", 0x3690000000000000ULL, 22, 0, 0x00000001U, 22, 1, 0x800000000000003cULL, 0x3f69, 22, 0),
	S("1.18973149535723176502e+4932", 0x7ff0000000000000ULL, 28, 1, 0x7f800000U, 28, 1, 0xffffffffffffffffULL, 0x7ffe, 28, 0),
	S("1.18973149535723176505e+4932", 0x7ff0000000000000ULL, 28, 1, 0x7f800000U, 28, 1, 0xffffffffffffffffULL, 0x7ffe, 28, 0),
	S("3.64519953188247460253e-4951", 0x0000000000000000ULL, 28, 1, 0x00000000U, 28, 1, 0x0000000000000001ULL, 0x0000, 28, 1),
	S("1.8e-4951", 0x0000000000000000ULL, 9, 1, 0x00000000U, 9, 1, 0x0000000000000000ULL, 0x0000, 9, 1),
	S("0.000000000000000000000000000000000000000000001e45", 0x3ff0000000000000ULL, 50, 0, 0x3f800000U, 50, 0, 0x8000000000000000ULL, 0x3fff, 50, 0),
	S("100000000000000000000000000000000000000000000000000000e-54", 0x3fb999999999999aULL, 58, 0, 0x3dcccccdU, 58, 0, 0xcccccccccccccccdULL, 0x3ffb, 58, 0),
	S("123456789012345678901234567890", 0x45f8ee90ff6c373eULL, 30, 0, 0x6fc77488U, 30, 0, 0xc77487fb61b9f077ULL, 0x405f, 30, 0),
	S("0.30000000000000004", 0x3fd3333333333334ULL, 19, 0, 0x3e99999aU, 19, 0, 0x9999999999999f5dULL, 0x3ffd, 19, 0),
	S("2.5e-1", 0x3fd0000000000000ULL, 6, 0, 0x3e800000U, 6, 0, 0x8000000000000000ULL, 0x3ffd, 6, 0),
};

static void floats(void)
{
	for (size_t i = 0; i < sizeof fcases / sizeof *fcases; i++) {
		const struct fcase *c = &fcases[i];
		char *e;
		errno = 0;
		double d = strtod(c->s, &e);
		int ok = (e - c->s == c->dend) && ((errno == ERANGE) == c->derr);
		uint64_t db;
		memcpy(&db, &d, 8);
		if (d != d)
			db = 0x7ff8000000000000ULL | (db & 0x8000000000000000ULL);
		ok &= db == c->d;

		errno = 0;
		float f = strtof(c->s, &e);
		ok &= (e - c->s == c->fend) && ((errno == ERANGE) == c->ferr);
		uint32_t fb;
		memcpy(&fb, &f, 4);
		if (f != f)
			fb = 0x7fc00000u | (fb & 0x80000000u);
		ok &= fb == c->f;

		errno = 0;
		long double l = strtold(c->s, &e);
		ok &= (e - c->s == c->lend) && ((errno == ERANGE) == c->lerr);
		uint64_t lm;
		uint16_t le;
		memcpy(&lm, &l, 8);
		memcpy(&le, (char *)&l + 8, 2);
		if (l != l)
			lm = 0xc000000000000000ULL;
		ok &= lm == c->lm && le == c->le;
		if (!ok) {
			t_fail(__FILE__, __LINE__, "strtod family");
			t_puts("  input \"");
			t_puts(c->s);
			t_puts("\"\n");
		}
	}
	CHECK(atof("2.5") == 2.5);
}

static void ints(void)
{
	char *e;
	errno = 0;
	CHECK(strtol("  -123abc", &e, 10) == -123 && *e == 'a' && errno == 0);
	CHECK(strtol("0x1F", &e, 0) == 31 && !*e);
	CHECK(strtol("0x1F", &e, 16) == 31 && !*e);
	CHECK(strtol("0x", &e, 16) == 0 && *e == 'x');
	CHECK(strtol("0xg", &e, 0) == 0 && *e == 'x');
	CHECK(strtol("017", &e, 0) == 15);
	CHECK(strtol("09", &e, 0) == 0 && *e == '9');
	CHECK(strtol("zz", &e, 36) == 35 * 36 + 35);
	CHECK(strtol("+", &e, 10) == 0 && e[-0] == '+');
	CHECK(strtol("   ", &e, 10) == 0 && *e == ' ');
	errno = 0;
	CHECK(strtol("9223372036854775807", 0, 10) == LONG_MAX && errno == 0);
	CHECK(strtol("-9223372036854775808", 0, 10) == LONG_MIN && errno == 0);
	CHECK(strtol("9223372036854775808", 0, 10) == LONG_MAX && errno == ERANGE);
	errno = 0;
	CHECK(strtol("-9223372036854775809", &e, 10) == LONG_MIN && errno == ERANGE && !*e);
	errno = 0;
	CHECK(strtol("99999999999999999999999999", &e, 10) == LONG_MAX && errno == ERANGE && !*e);
	errno = 0;
	CHECK(strtoul("18446744073709551615", 0, 10) == ULONG_MAX && errno == 0);
	CHECK(strtoul("18446744073709551616", 0, 10) == ULONG_MAX && errno == ERANGE);
	errno = 0;
	CHECK(strtoul("-1", 0, 10) == ULONG_MAX && errno == 0);
	CHECK(strtoull("-2", 0, 10) == ULLONG_MAX - 1);
	CHECK(strtoimax("-42", 0, 10) == -42 && strtoumax("42", 0, 10) == 42);
	errno = 0;
	CHECK(strtol("1", &e, 1) == 0 && errno == EINVAL);
	errno = 0;
	CHECK(strtol("1", &e, 37) == 0 && errno == EINVAL);
	CHECK(atoi("  42x") == 42 && atoi("99999999999") == INT_MAX && atol("-5") == -5 && atoll("7") == 7);

	const char *err;
	CHECK(strtonum("123", 0, 1000, &err) == 123 && err == 0);
	CHECK(strtonum("1001", 0, 1000, &err) == 0 && !strcmp(err, "too large") && errno == ERANGE);
	CHECK(strtonum("-1", 0, 1000, &err) == 0 && !strcmp(err, "too small"));
	CHECK(strtonum("12x", 0, 1000, &err) == 0 && !strcmp(err, "invalid") && errno == EINVAL);
	CHECK(strtonum("", 0, 1000, &err) == 0 && !strcmp(err, "invalid"));
	CHECK(strtonum("5", 10, 1, &err) == 0 && !strcmp(err, "invalid"));
	CHECK(strtonum("99999999999999999999", 0, LLONG_MAX, &err) == 0 && !strcmp(err, "too large"));

	CHECK(abs(-3) == 3 && labs(-3L) == 3 && llabs(-3LL) == 3 && imaxabs(-3) == 3);
	div_t q = div(-7, 2);
	CHECK(q.quot == -3 && q.rem == -1);
	lldiv_t lq = lldiv(7, -2);
	CHECK(lq.quot == -3 && lq.rem == 1);
}

int main(void)
{
	floats();
	ints();
	return t_done();
}
