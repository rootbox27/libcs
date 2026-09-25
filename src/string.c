/* String and memory functions, plus the _FORTIFY_SOURCE checked variants
 * and constant-time helpers. */
#include "internal.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#define ONES ((size_t)-1 / UCHAR_MAX)
#define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
#define HASZERO(x) (((x) - ONES) & ~(x) & HIGHS)
typedef size_t __attribute__((__may_alias__)) word;

void *memcpy(void *__restrict d, const void *__restrict s, size_t n)
{
	void *r = d;
	__asm__ __volatile__("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
	return r;
}

void *memmove(void *d, const void *s, size_t n)
{
	if ((uintptr_t)d - (uintptr_t)s >= n)
		return memcpy(d, s, n);
	/* overlapping with d > s: copy backwards */
	unsigned char *dd = (unsigned char *)d + n;
	const unsigned char *ss = (const unsigned char *)s + n;
	while (n >= sizeof(word) && ((uintptr_t)dd % sizeof(word))) {
		*--dd = *--ss;
		n--;
	}
	while (n >= sizeof(word)) {
		dd -= sizeof(word); ss -= sizeof(word); n -= sizeof(word);
		word w;
		__builtin_memcpy(&w, ss, sizeof w);
		*(word *)dd = w;
	}
	while (n--)
		*--dd = *--ss;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	void *r = d;
	__asm__ __volatile__("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
	return r;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *l = a, *r = b;
	for (; n && *l == *r; n--, l++, r++) ;
	return n ? *l - *r : 0;
}
int bcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
void bcopy(const void *s, void *d, size_t n) { memmove(d, s, n); }
void bzero(void *d, size_t n) { memset(d, 0, n); }

void *memchr(const void *src, int c, size_t n)
{
	const unsigned char *s = src;
	c = (unsigned char)c;
	for (; ((uintptr_t)s & (sizeof(word) - 1)) && n && *s != c; s++, n--) ;
	if (n && *s != c) {
		const word *w;
		size_t k = ONES * c;
		for (w = (const void *)s; n >= sizeof(word) && !HASZERO(*w ^ k); w++, n -= sizeof(word)) ;
		s = (const void *)w;
	}
	for (; n && *s != c; s++, n--) ;
	return n ? (void *)s : 0;
}

void *memrchr(const void *m, int c, size_t n)
{
	const unsigned char *s = m;
	c = (unsigned char)c;
	while (n--)
		if (s[n] == c)
			return (void *)(s + n);
	return 0;
}

void *mempcpy(void *__restrict d, const void *__restrict s, size_t n)
{
	return (char *)memcpy(d, s, n) + n;
}

void *memccpy(void *__restrict d, const void *__restrict s, int c, size_t n)
{
	unsigned char *dd = d;
	const unsigned char *ss = s;
	c = (unsigned char)c;
	for (; n; n--, dd++, ss++)
		if ((*dd = *ss) == c)
			return dd + 1;
	return 0;
}

void *memmem(const void *h0, size_t k, const void *n0, size_t l)
{
	const unsigned char *h = h0, *n = n0;
	if (!l)
		return (void *)h;
	if (k < l)
		return 0;
	const unsigned char *end = h + (k - l) + 1;
	for (; h < end; h++) {
		h = memchr(h, *n, (size_t)(end - h));
		if (!h)
			return 0;
		if (!memcmp(h, n, l))
			return (void *)h;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *a = s;
	const word *w;
	for (; (uintptr_t)s % sizeof(word); s++)
		if (!*s)
			return (size_t)(s - a);
	for (w = (const void *)s; !HASZERO(*w); w++) ;
	s = (const void *)w;
	for (; *s; s++) ;
	return (size_t)(s - a);
}

size_t strnlen(const char *s, size_t n)
{
	const char *p = memchr(s, 0, n);
	return p ? (size_t)(p - s) : n;
}

char *stpcpy(char *__restrict d, const char *__restrict s)
{
	size_t n = strlen(s);
	memcpy(d, s, n + 1);
	return d + n;
}
char *strcpy(char *__restrict d, const char *__restrict s)
{
	stpcpy(d, s);
	return d;
}

char *stpncpy(char *__restrict d, const char *__restrict s, size_t n)
{
	size_t l = strnlen(s, n);
	memcpy(d, s, l);
	memset(d + l, 0, n - l);
	return d + l;
}
char *strncpy(char *__restrict d, const char *__restrict s, size_t n)
{
	stpncpy(d, s, n);
	return d;
}

char *strcat(char *__restrict d, const char *__restrict s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strncat(char *__restrict d, const char *__restrict s, size_t n)
{
	char *a = d;
	d += strlen(d);
	size_t l = strnlen(s, n);
	memcpy(d, s, l);
	d[l] = 0;
	return a;
}

size_t strlcpy(char *__restrict d, const char *__restrict s, size_t n)
{
	size_t l = strlen(s);
	if (n) {
		size_t c = l < n - 1 ? l : n - 1;
		memcpy(d, s, c);
		d[c] = 0;
	}
	return l;
}

size_t strlcat(char *__restrict d, const char *__restrict s, size_t n)
{
	size_t l = strnlen(d, n);
	if (l == n)
		return l + strlen(s);
	return l + strlcpy(d + l, s, n - l);
}

int strcmp(const char *l, const char *r)
{
	for (; *l == *r && *l; l++, r++) ;
	return *(const unsigned char *)l - *(const unsigned char *)r;
}

int strncmp(const char *l, const char *r, size_t n)
{
	if (!n--)
		return 0;
	for (; *l && *r && n && *l == *r; l++, r++, n--) ;
	return *(const unsigned char *)l - *(const unsigned char *)r;
}

int strcasecmp(const char *l, const char *r)
{
	const unsigned char *a = (const void *)l, *b = (const void *)r;
	for (; *a && *b && (*a == *b || tolower(*a) == tolower(*b)); a++, b++) ;
	return tolower(*a) - tolower(*b);
}

int strncasecmp(const char *l, const char *r, size_t n)
{
	const unsigned char *a = (const void *)l, *b = (const void *)r;
	if (!n--)
		return 0;
	for (; *a && *b && n && (*a == *b || tolower(*a) == tolower(*b)); a++, b++, n--) ;
	return tolower(*a) - tolower(*b);
}

int strcoll(const char *l, const char *r) { return strcmp(l, r); }

size_t strxfrm(char *__restrict d, const char *__restrict s, size_t n)
{
	size_t l = strlen(s);
	if (n > l)
		strcpy(d, s);
	return l;
}

char *strchrnul(const char *s, int c)
{
	c = (unsigned char)c;
	if (!c)
		return (char *)s + strlen(s);
	for (; (uintptr_t)s % sizeof(word); s++)
		if (!*s || *(const unsigned char *)s == c)
			return (char *)s;
	size_t k = ONES * c;
	const word *w;
	for (w = (const void *)s; !HASZERO(*w) && !HASZERO(*w ^ k); w++) ;
	s = (const void *)w;
	for (; *s && *(const unsigned char *)s != c; s++) ;
	return (char *)s;
}

char *strchr(const char *s, int c)
{
	char *r = strchrnul(s, c);
	return *(unsigned char *)r == (unsigned char)c ? r : 0;
}
char *index(const char *s, int c) { return strchr(s, c); }

char *strrchr(const char *s, int c)
{
	return memrchr(s, c, strlen(s) + 1);
}
char *rindex(const char *s, int c) { return strrchr(s, c); }

#define BITOP(a, b, op) ((a)[(size_t)(b) / (8 * sizeof *(a))] op (size_t)1 << ((size_t)(b) % (8 * sizeof *(a))))

size_t strspn(const char *s, const char *c)
{
	const char *a = s;
	size_t byteset[32 / sizeof(size_t)] = { 0 };
	if (!c[0])
		return 0;
	if (!c[1]) {
		for (; *s == *c; s++) ;
		return (size_t)(s - a);
	}
	for (; *c && BITOP(byteset, *(const unsigned char *)c, |=); c++) ;
	for (; *s && BITOP(byteset, *(const unsigned char *)s, &); s++) ;
	return (size_t)(s - a);
}

size_t strcspn(const char *s, const char *c)
{
	const char *a = s;
	size_t byteset[32 / sizeof(size_t)];
	if (!c[0] || !c[1])
		return (size_t)(strchrnul(s, *c) - a);
	memset(byteset, 0, sizeof byteset);
	for (; *c && BITOP(byteset, *(const unsigned char *)c, |=); c++) ;
	for (; *s && !BITOP(byteset, *(const unsigned char *)s, &); s++) ;
	return (size_t)(s - a);
}

char *strpbrk(const char *s, const char *b)
{
	s += strcspn(s, b);
	return *s ? (char *)s : 0;
}

char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	if (!l)
		return (char *)h;
	return memmem(h, strlen(h), n, l);
}

char *strcasestr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++)
		if (!strncasecmp(h, n, l))
			return (char *)h;
	return l ? 0 : (char *)h;
}

char *strtok_r(char *__restrict s, const char *__restrict sep, char **__restrict p)
{
	if (!s && !(s = *p))
		return 0;
	s += strspn(s, sep);
	if (!*s)
		return *p = 0;
	*p = s + strcspn(s, sep);
	if (**p)
		*(*p)++ = 0;
	else
		*p = 0;
	return s;
}

char *strtok(char *__restrict s, const char *__restrict sep)
{
	static __thread char *p;
	return strtok_r(s, sep, &p);
}

char *strsep(char **str, const char *sep)
{
	char *s = *str, *end;
	if (!s)
		return 0;
	end = s + strcspn(s, sep);
	if (*end)
		*end++ = 0;
	else
		end = 0;
	*str = end;
	return s;
}

char *strdup(const char *s)
{
	size_t l = strlen(s);
	char *d = malloc(l + 1);
	return d ? memcpy(d, s, l + 1) : 0;
}

char *strndup(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	char *d = malloc(l + 1);
	if (!d)
		return 0;
	memcpy(d, s, l);
	d[l] = 0;
	return d;
}

int strverscmp(const char *l0, const char *r0)
{
	const unsigned char *l = (const void *)l0, *r = (const void *)r0;
	size_t i, dp, j;
	int z = 1;
	for (dp = i = 0; l[i] == r[i]; i++) {
		int c = l[i];
		if (!c)
			return 0;
		if (!isdigit(c))
			dp = i + 1, z = 1;
		else if (c != '0')
			z = 0;
	}
	if ((unsigned)(l[dp] - '1') < 9 && (unsigned)(r[dp] - '1') < 9) {
		for (j = i; isdigit(l[j]); j++)
			if (!isdigit(r[j]))
				return 1;
		if (isdigit(r[j]))
			return -1;
	} else if (z && dp < i && (isdigit(l[i]) || isdigit(r[i]))) {
		return (unsigned char)(l[i] - '0') - (unsigned char)(r[i] - '0');
	}
	return l[i] - r[i];
}

/* explicit_bzero: the compiler barrier keeps the stores from being
 * eliminated as dead. */
void explicit_bzero(void *d, size_t n)
{
	memset(d, 0, n);
	__asm__ __volatile__("" : : "r"(d) : "memory");
}

int timingsafe_bcmp(const void *a, const void *b, size_t n)
{
	const volatile unsigned char *p = a, *q = b;
	unsigned char r = 0;
	for (size_t i = 0; i < n; i++)
		r |= p[i] ^ q[i];
	return r != 0;
}

int timingsafe_memcmp(const void *a, const void *b, size_t n)
{
	const volatile unsigned char *p = a, *q = b;
	int res = 0, done = 0;
	for (size_t i = 0; i < n; i++) {
		int lt = (p[i] - q[i]) >> 8;          /* -1 if p<q */
		int gt = (q[i] - p[i]) >> 8;          /* -1 if p>q */
		int cmp = lt - gt;                    /* -1, 1, or 0 */
		res |= cmp & ~done;
		done |= lt | gt;
	}
	return res;
}

int ffs(int i) { return __builtin_ffs(i); }
int ffsl(long i) { return __builtin_ffsl(i); }
int ffsll(long long i) { return __builtin_ffsll(i); }

/* ---- _FORTIFY_SOURCE entry points ---- */
void *__memcpy_chk(void *d, const void *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return memcpy(d, s, n);
}
void *__memmove_chk(void *d, const void *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return memmove(d, s, n);
}
void *__memset_chk(void *d, int c, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return memset(d, c, n);
}
void *__mempcpy_chk(void *d, const void *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return mempcpy(d, s, n);
}
char *__strcpy_chk(char *d, const char *s, size_t dl)
{
	size_t n = strlen(s) + 1;
	if (n > dl)
		__chk_fail();
	return memcpy(d, s, n);
}
char *__stpcpy_chk(char *d, const char *s, size_t dl)
{
	size_t n = strlen(s);
	if (n + 1 > dl)
		__chk_fail();
	memcpy(d, s, n + 1);
	return d + n;
}
char *__strncpy_chk(char *d, const char *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return strncpy(d, s, n);
}
char *__stpncpy_chk(char *d, const char *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return stpncpy(d, s, n);
}
char *__strcat_chk(char *d, const char *s, size_t dl)
{
	size_t a = strnlen(d, dl), b = strlen(s);
	if (a == dl || a + b + 1 > dl)
		__chk_fail();
	memcpy(d + a, s, b + 1);
	return d;
}
char *__strncat_chk(char *d, const char *s, size_t n, size_t dl)
{
	size_t a = strnlen(d, dl), b = strnlen(s, n);
	if (a == dl || a + b + 1 > dl)
		__chk_fail();
	memcpy(d + a, s, b);
	d[a + b] = 0;
	return d;
}
size_t __strlcpy_chk(char *d, const char *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return strlcpy(d, s, n);
}
size_t __strlcat_chk(char *d, const char *s, size_t n, size_t dl)
{
	if (n > dl)
		__chk_fail();
	return strlcat(d, s, n);
}
