/* Wide character classification and width, from generated Unicode tables. */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "unicode_tables.h"

static int in(const uint32_t (*r)[2], size_t n, wint_t c)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (c < r[mid][0])
			hi = mid;
		else if (c > r[mid][1])
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}
#define IN(tab, c) in(tab, sizeof tab / sizeof *tab, c)

int iswalpha(wint_t c) { return c < 0x80 ? ((c | 32) - 'a' < 26) : IN(u_alpha, c); }
int iswdigit(wint_t c) { return c - '0' < 10; }
int iswalnum(wint_t c) { return iswdigit(c) || iswalpha(c); }
int iswupper(wint_t c) { return c < 0x80 ? c - 'A' < 26 : IN(u_upper, c); }
int iswlower(wint_t c) { return c < 0x80 ? c - 'a' < 26 : IN(u_lower, c); }
int iswspace(wint_t c) { return IN(u_space, c); }
int iswblank(wint_t c) { return IN(u_blank, c); }
int iswcntrl(wint_t c) { return IN(u_cntrl, c); }
int iswpunct(wint_t c) { return c < 0x80 ? (c - 0x21 < 0x5e && !iswalnum(c)) : IN(u_punct, c); }
int iswgraph(wint_t c) { return c < 0x80 ? c - 0x21 < 0x5e : IN(u_graph, c); }
int iswprint(wint_t c) { return c < 0x80 ? c - 0x20 < 0x5f : IN(u_print, c); }
int iswxdigit(wint_t c) { return c - '0' < 10 || (c | 32) - 'a' < 6; }

static wint_t map(wint_t c, const void *tab, size_t n)
{
	const struct { uint32_t start, end; int32_t delta; uint8_t step; } *t = tab;
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (c < t[mid].start)
			hi = mid;
		else if (c > t[mid].end)
			lo = mid + 1;
		else
			return (c - t[mid].start) % t[mid].step ? c : (wint_t)((int32_t)c + t[mid].delta);
	}
	return c;
}

wint_t towlower(wint_t c)
{
	if (c < 0x80)
		return c - 'A' < 26 ? c | 32 : c;
	return map(c, u_tolower, sizeof u_tolower / sizeof *u_tolower);
}

wint_t towupper(wint_t c)
{
	if (c < 0x80)
		return c - 'a' < 26 ? c & 0x5f : c;
	return map(c, u_toupper, sizeof u_toupper / sizeof *u_toupper);
}

static const char *const classes[] = {
	"alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print", "punct", "space", "upper", "xdigit",
};
static int (*const class_fn[])(wint_t) = {
	iswalnum, iswalpha, iswblank, iswcntrl, iswdigit, iswgraph, iswlower, iswprint, iswpunct, iswspace, iswupper, iswxdigit,
};

wctype_t wctype(const char *name)
{
	for (size_t i = 0; i < sizeof classes / sizeof *classes; i++)
		if (!strcmp(name, classes[i]))
			return i + 1;
	return 0;
}

int iswctype(wint_t c, wctype_t t)
{
	if (!t || t > sizeof classes / sizeof *classes)
		return 0;
	return class_fn[t - 1](c);
}

int wcwidth(wchar_t wc)
{
	unsigned c = (unsigned)wc;
	if (c - 0x20 < 0x5f)
		return 1;
	if (c >= 0x110000 || IN(u_nonprint, c))
		return c ? -1 : 0;
	if (IN(u_width0, c))
		return 0;
	return IN(u_width2, c) ? 2 : 1;
}

int wcswidth(const wchar_t *s, size_t n)
{
	int w = 0;
	for (; n && *s; n--, s++) {
		int k = wcwidth(*s);
		if (k < 0)
			return -1;
		if (w > INT_MAX - k)
			return -1;
		w += k;
	}
	return w;
}

/* wctrans: the two mappings C defines */
wctrans_t wctrans(const char *name)
{
	if (!strcmp(name, "tolower"))
		return 1;
	if (!strcmp(name, "toupper"))
		return 2;
	return 0;
}

wint_t towctrans(wint_t c, wctrans_t t)
{
	return t == 1 ? towlower(c) : t == 2 ? towupper(c) : c;
}
