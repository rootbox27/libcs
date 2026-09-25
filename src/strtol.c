/* Integer parsing: strtol family, strto[iu]max, ato*, strtonum. */
#include "internal.h"
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

static int digit_val(int c)
{
	if ((unsigned)c - '0' < 10)
		return c - '0';
	c |= 32;
	if ((unsigned)c - 'a' < 26)
		return c - 'a' + 10;
	return 99;
}

/* Parse an integer. For signed results max is the type's maximum and the
 * magnitude may reach max + 1 when negative; for unsigned results a
 * leading '-' negates the value in the unsigned type, as C requires. */
static unsigned long long strtox(const char *s0, char **end, int base, int is_signed, unsigned long long max)
{
	const unsigned char *s = (const unsigned char *)s0;
	if (base < 0 || base == 1 || base > 36) {
		if (end)
			*end = (char *)s0;
		errno = EINVAL;
		return 0;
	}
	while (isspace(*s))
		s++;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x' && digit_val(s[2]) < 16) {
		s += 2;
		base = 16;
	} else if (base == 0) {
		base = *s == '0' ? 8 : 10;
	}
	const unsigned char *start = s;
	unsigned long long acc = 0;
	int ovf = 0;
	for (int d; (d = digit_val(*s)) < base; s++) {
		if (__builtin_mul_overflow(acc, (unsigned)base, &acc) || __builtin_add_overflow(acc, (unsigned)d, &acc))
			ovf = 1;
	}
	if (end)
		*end = (char *)(s == start ? (const unsigned char *)s0 : s);
	if (s == start)
		return 0;
	if (is_signed) {
		unsigned long long lim = neg ? max + 1 : max;
		if (ovf || acc > lim) {
			errno = ERANGE;
			return neg ? -max - 1 : max;
		}
	} else if (ovf || acc > max) {
		errno = ERANGE;
		return max;
	}
	return neg ? -acc : acc;
}

long strtol(const char *__restrict s, char **__restrict end, int base)
{
	return (long)strtox(s, end, base, 1, LONG_MAX);
}
unsigned long strtoul(const char *__restrict s, char **__restrict end, int base)
{
	return strtox(s, end, base, 0, ULONG_MAX);
}
long long strtoll(const char *__restrict s, char **__restrict end, int base)
{
	return (long long)strtox(s, end, base, 1, LLONG_MAX);
}
unsigned long long strtoull(const char *__restrict s, char **__restrict end, int base)
{
	return strtox(s, end, base, 0, ULLONG_MAX);
}
intmax_t strtoimax(const char *__restrict s, char **__restrict end, int base)
{
	return (intmax_t)strtox(s, end, base, 1, INTMAX_MAX);
}
uintmax_t strtoumax(const char *__restrict s, char **__restrict end, int base)
{
	return strtox(s, end, base, 0, UINTMAX_MAX);
}

/* The ato* functions are strtol without error reporting; out-of-range
 * input saturates instead of being undefined. */
int atoi(const char *s)
{
	long v = strtol(s, 0, 10);
	return v > INT_MAX ? INT_MAX : v < INT_MIN ? INT_MIN : (int)v;
}
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }

/* OpenBSD strtonum: whole-string base-10 parse with range check. */
long long strtonum(const char *s, long long minval, long long maxval, const char **errstr)
{
	const char *err = 0;
	long long v = 0;
	int e = errno;
	if (minval > maxval) {
		err = "invalid";
		errno = EINVAL;
	} else {
		char *end;
		errno = 0;
		v = strtoll(s, &end, 10);
		if (end == s || *end) {
			err = "invalid";
			errno = EINVAL;
		} else if ((v == LLONG_MIN && errno == ERANGE) || v < minval) {
			err = "too small";
			errno = ERANGE;
		} else if ((v == LLONG_MAX && errno == ERANGE) || v > maxval) {
			err = "too large";
			errno = ERANGE;
		} else {
			errno = e;
		}
	}
	if (errstr)
		*errstr = err;
	return err ? 0 : v;
}

int abs(int a) { return a < 0 ? -a : a; }
long labs(long a) { return a < 0 ? -a : a; }
long long llabs(long long a) { return a < 0 ? -a : a; }
intmax_t imaxabs(intmax_t a) { return a < 0 ? -a : a; }
div_t div(int n, int d) { return (div_t){ n / d, n % d }; }
ldiv_t ldiv(long n, long d) { return (ldiv_t){ n / d, n % d }; }
lldiv_t lldiv(long long n, long long d) { return (lldiv_t){ n / d, n % d }; }
imaxdiv_t imaxdiv(intmax_t n, intmax_t d) { return (imaxdiv_t){ n / d, n % d }; }
