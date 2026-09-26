/* Wide-string numeric conversions. Only ASCII characters can be part of
 * a number, so after the leading wide white space the whole run of ASCII
 * characters is narrowed (however long: correct rounding of wcstod needs
 * every digit) and handed to the byte function; the end pointer maps back
 * one to one. */
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

struct nar {
	char *p;
	char small[128];
};

/* Narrow the ASCII run at s; 0 on allocation failure. */
static char *narrow(struct nar *b, const wchar_t *s)
{
	size_t n = 0;
	while (s[n] && (unsigned)s[n] < 0x80)
		n++;
	b->p = n < sizeof b->small ? b->small : malloc(n + 1);
	if (!b->p)
		return 0;
	for (size_t i = 0; i < n; i++)
		b->p[i] = (char)s[i];
	b->p[n] = 0;
	return b->p;
}

static void done(struct nar *b)
{
	if (b->p != b->small)
		free(b->p);
}

/* The body shared by every conversion; `call` sets e. */
#define BODY(T, call) \
	const wchar_t *t = s; \
	while (iswspace((wint_t)*t)) \
		t++; \
	struct nar b; \
	char *e; \
	if (!narrow(&b, t)) { \
		if (end) \
			*end = (wchar_t *)s; \
		return 0; \
	} \
	T v = call; \
	if (end) \
		*end = (wchar_t *)(e == b.p ? s : t + (e - b.p)); \
	done(&b); \
	return v;

#define CONVI(T, name, fn) \
	T name(const wchar_t *restrict s, wchar_t **restrict end, int base) { BODY(T, fn(b.p, &e, base)) }
#define CONVF(T, name, fn) \
	T name(const wchar_t *restrict s, wchar_t **restrict end) { BODY(T, fn(b.p, &e)) }

CONVI(long, wcstol, strtol)
CONVI(unsigned long, wcstoul, strtoul)
CONVI(long long, wcstoll, strtoll)
CONVI(unsigned long long, wcstoull, strtoull)
CONVI(intmax_t, wcstoimax, strtoimax)
CONVI(uintmax_t, wcstoumax, strtoumax)
CONVF(float, wcstof, strtof)
CONVF(double, wcstod, strtod)
CONVF(long double, wcstold, strtold)
