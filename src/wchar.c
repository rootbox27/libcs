/* Wide-character string functions. */
#include "internal.h"
#include <stdlib.h>
#include <wchar.h>

size_t wcslen(const wchar_t *s)
{
	const wchar_t *a = s;
	while (*s)
		s++;
	return (size_t)(s - a);
}

size_t wcsnlen(const wchar_t *s, size_t n)
{
	size_t i = 0;
	while (i < n && s[i])
		i++;
	return i;
}

wchar_t *wcscpy(wchar_t *__restrict d, const wchar_t *__restrict s)
{
	wchar_t *a = d;
	while ((*d++ = *s++)) ;
	return a;
}

wchar_t *wcsncpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	wchar_t *a = d;
	for (; n && *s; n--)
		*d++ = *s++;
	for (; n; n--)
		*d++ = 0;
	return a;
}

wchar_t *wcscat(wchar_t *__restrict d, const wchar_t *__restrict s)
{
	wcscpy(d + wcslen(d), s);
	return d;
}

int wcscmp(const wchar_t *l, const wchar_t *r)
{
	for (; *l == *r && *l; l++, r++) ;
	return *l < *r ? -1 : *l > *r;
}

int wcsncmp(const wchar_t *l, const wchar_t *r, size_t n)
{
	for (; n && *l == *r && *l; n--, l++, r++) ;
	return n ? (*l < *r ? -1 : *l > *r) : 0;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
	for (;; s++) {
		if (*s == c)
			return (wchar_t *)s;
		if (!*s)
			return 0;
	}
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
	const wchar_t *r = 0;
	for (;; s++) {
		if (*s == c)
			r = s;
		if (!*s)
			return (wchar_t *)r;
	}
}

wchar_t *wcsdup(const wchar_t *s)
{
	size_t l = wcslen(s) + 1;
	wchar_t *d = reallocarray(0, l, sizeof *d);
	return d ? wmemcpy(d, s, l) : 0;
}

wchar_t *wmemcpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	wchar_t *a = d;
	while (n--)
		*d++ = *s++;
	return a;
}

wchar_t *wmemmove(wchar_t *d, const wchar_t *s, size_t n)
{
	wchar_t *a = d;
	if ((uintptr_t)d - (uintptr_t)s >= n * sizeof *d) {
		while (n--)
			*d++ = *s++;
	} else {
		while (n--)
			d[n] = s[n];
	}
	return a;
}

wchar_t *wmemset(wchar_t *d, wchar_t c, size_t n)
{
	wchar_t *a = d;
	while (n--)
		*d++ = c;
	return a;
}

int wmemcmp(const wchar_t *l, const wchar_t *r, size_t n)
{
	for (; n && *l == *r; n--, l++, r++) ;
	return n ? (*l < *r ? -1 : *l > *r) : 0;
}

wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
	for (; n; n--, s++)
		if (*s == c)
			return (wchar_t *)s;
	return 0;
}

size_t wcsspn(const wchar_t *s, const wchar_t *c)
{
	const wchar_t *a = s;
	for (; *s && wcschr(c, *s); s++) ;
	return (size_t)(s - a);
}

size_t wcscspn(const wchar_t *s, const wchar_t *c)
{
	const wchar_t *a = s;
	for (; *s && !wcschr(c, *s); s++) ;
	return (size_t)(s - a);
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *c)
{
	s += wcscspn(s, c);
	return *s ? (wchar_t *)s : 0;
}

wchar_t *wcsncat(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	wchar_t *a = d;
	d += wcslen(d);
	while (n-- && *s)
		*d++ = *s++;
	*d = 0;
	return a;
}

wchar_t *wcstok(wchar_t *__restrict s, const wchar_t *__restrict sep, wchar_t **__restrict p)
{
	if (!s && !(s = *p))
		return 0;
	s += wcsspn(s, sep);
	if (!*s)
		return *p = 0;
	*p = s + wcscspn(s, sep);
	if (**p)
		*(*p)++ = 0;
	else
		*p = 0;
	return s;
}

/* Knuth-Morris-Pratt: linear in the haystack whatever the needle, so a
 * hostile needle cannot make the search quadratic. */
wchar_t *wcsstr(const wchar_t *__restrict h, const wchar_t *__restrict n)
{
	size_t m = wcslen(n);
	if (!m)
		return (wchar_t *)h;
	size_t small[64], *fail = m <= 64 ? small : malloc(m * sizeof *fail);
	if (!fail) {
		/* no memory for the table: the plain search is still correct */
		for (; *h; h++)
			if (!wcsncmp(h, n, m))
				return (wchar_t *)h;
		return 0;
	}
	fail[0] = 0;
	for (size_t i = 1, k = 0; i < m; i++) {
		while (k && n[i] != n[k])
			k = fail[k - 1];
		if (n[i] == n[k])
			k++;
		fail[i] = k;
	}
	wchar_t *r = 0;
	for (size_t k = 0; *h; h++) {
		while (k && *h != n[k])
			k = fail[k - 1];
		if (*h == n[k] && ++k == m) {
			r = (wchar_t *)h - (m - 1);
			break;
		}
	}
	if (fail != small)
		free(fail);
	return r;
}

/* The C locale collates by code point. */
int wcscoll(const wchar_t *l, const wchar_t *r)
{
	return wcscmp(l, r);
}

size_t wcsxfrm(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n)
{
	size_t l = wcslen(s);
	if (l < n)
		wmemcpy(d, s, l + 1);
	return l;
}
