/* The POSIX 2008 *_l functions. The only locale is C (UTF-8), so each
 * one is its locale-free counterpart. Also mbsnrtowcs, wcsnrtombs and the
 * wide case-insensitive comparisons. */
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#define L1(ret, name, T) ret name##_l(T c, locale_t l) { return name(c); }
L1(int, isalnum, int) L1(int, isalpha, int) L1(int, isblank, int) L1(int, iscntrl, int)
L1(int, isdigit, int) L1(int, isgraph, int) L1(int, islower, int) L1(int, isprint, int)
L1(int, ispunct, int) L1(int, isspace, int) L1(int, isupper, int) L1(int, isxdigit, int)
L1(int, tolower, int) L1(int, toupper, int)
L1(int, iswalnum, wint_t) L1(int, iswalpha, wint_t) L1(int, iswblank, wint_t) L1(int, iswcntrl, wint_t)
L1(int, iswdigit, wint_t) L1(int, iswgraph, wint_t) L1(int, iswlower, wint_t) L1(int, iswprint, wint_t)
L1(int, iswpunct, wint_t) L1(int, iswspace, wint_t) L1(int, iswupper, wint_t) L1(int, iswxdigit, wint_t)
L1(wint_t, towlower, wint_t) L1(wint_t, towupper, wint_t)
L1(wctype_t, wctype, const char *) L1(wctrans_t, wctrans, const char *)
L1(char *, strerror, int)

int iswctype_l(wint_t c, wctype_t t, locale_t l) { return iswctype(c, t); }
wint_t towctrans_l(wint_t c, wctrans_t t, locale_t l) { return towctrans(c, t); }

int strcoll_l(const char *a, const char *b, locale_t l) { return strcoll(a, b); }
size_t strxfrm_l(char *__restrict d, const char *__restrict s, size_t n, locale_t l) { return strxfrm(d, s, n); }
int wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t l) { return wcscoll(a, b); }
size_t wcsxfrm_l(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n, locale_t l) { return wcsxfrm(d, s, n); }
int strcasecmp_l(const char *a, const char *b, locale_t l) { return strcasecmp(a, b); }
int strncasecmp_l(const char *a, const char *b, size_t n, locale_t l) { return strncasecmp(a, b, n); }

size_t strftime_l(char *__restrict s, size_t n, const char *__restrict f, const struct tm *__restrict tm, locale_t l)
{
	return strftime(s, n, f, tm);
}

float strtof_l(const char *__restrict s, char **__restrict e, locale_t l) { return strtof(s, e); }
double strtod_l(const char *__restrict s, char **__restrict e, locale_t l) { return strtod(s, e); }
long double strtold_l(const char *__restrict s, char **__restrict e, locale_t l) { return strtold(s, e); }
long strtol_l(const char *__restrict s, char **__restrict e, int b, locale_t l) { return strtol(s, e, b); }
unsigned long strtoul_l(const char *__restrict s, char **__restrict e, int b, locale_t l) { return strtoul(s, e, b); }
long long strtoll_l(const char *__restrict s, char **__restrict e, int b, locale_t l) { return strtoll(s, e, b); }
unsigned long long strtoull_l(const char *__restrict s, char **__restrict e, int b, locale_t l) { return strtoull(s, e, b); }

int wcscasecmp(const wchar_t *a, const wchar_t *b)
{
	return wcsncasecmp(a, b, (size_t)-1);
}

int wcsncasecmp(const wchar_t *a, const wchar_t *b, size_t n)
{
	for (; n; n--, a++, b++) {
		wint_t x = towlower((wint_t)*a), y = towlower((wint_t)*b);
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			break;
	}
	return 0;
}

int wcscasecmp_l(const wchar_t *a, const wchar_t *b, locale_t l) { return wcscasecmp(a, b); }
int wcsncasecmp_l(const wchar_t *a, const wchar_t *b, size_t n, locale_t l) { return wcsncasecmp(a, b, n); }

/* mbsrtowcs reading at most n bytes. A character cut off by the limit
 * stays in the state for the next call. */
size_t mbsnrtowcs(wchar_t *__restrict dst, const char **__restrict src, size_t n, size_t len,
                  mbstate_t *__restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	const char *s = *src;
	size_t cnt = 0;
	while (!dst || cnt < len) {
		wchar_t wc;
		size_t r = mbrtowc(&wc, s, n, st);
		if (r == (size_t)-1) {
			if (dst)
				*src = s;
			return (size_t)-1;
		}
		if (r == (size_t)-2) {
			s += n;
			break;
		}
		if (r == 0) {
			if (dst) {
				dst[cnt] = 0;
				*src = 0;
			}
			return cnt;
		}
		if (dst)
			dst[cnt] = wc;
		cnt++;
		s += r;
		n -= r;
	}
	if (dst)
		*src = s;
	return cnt;
}

/* wcsrtombs converting at most n wide characters; only whole characters
 * are written to dst. */
size_t wcsnrtombs(char *__restrict dst, const wchar_t **__restrict src, size_t n, size_t len,
                  mbstate_t *__restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	const wchar_t *ws = *src;
	size_t cnt = 0;
	for (; n; n--, ws++) {
		char b[MB_LEN_MAX];
		size_t l = wcrtomb(b, *ws, st);
		if (l == (size_t)-1) {
			if (dst)
				*src = ws;
			return (size_t)-1;
		}
		if (dst) {
			if (l > len - cnt)
				break;
			memcpy(dst + cnt, b, l);
		}
		if (!*ws) {
			if (dst)
				*src = 0;
			return cnt;
		}
		cnt += l;
	}
	if (dst)
		*src = ws;
	return cnt;
}
