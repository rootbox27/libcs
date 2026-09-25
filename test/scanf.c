#include "harness.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat"
#else
#pragma GCC diagnostic ignored "-Wformat"
#endif

static void ints(void)
{
	int a = 0, b = 0, n = 0;
	unsigned u = 0;
	long l = 0;
	long long ll = 0;
	short h = 0;
	signed char hh = 0;
	CHECK(sscanf("12 -34", "%d %d", &a, &b) == 2 && a == 12 && b == -34);
	CHECK(sscanf("  +7x", "%d%n", &a, &n) == 1 && a == 7 && n == 4);
	CHECK(sscanf("0x1f 017 10", "%i %i %i", &a, &b, &n) == 3 && a == 31 && b == 15 && n == 10);
	CHECK(sscanf("ff", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("0XfF", "%x", &u) == 1 && u == 255);
	CHECK(sscanf("777", "%o", &u) == 1 && u == 511);
	CHECK(sscanf("12345", "%3d%d", &a, &b) == 2 && a == 123 && b == 45);
	CHECK(sscanf("-9223372036854775808", "%ld", &l) == 1 && l == (-9223372036854775807L - 1));
	CHECK(sscanf("123456789012", "%lld", &ll) == 1 && ll == 123456789012LL);
	CHECK(sscanf("70000 300", "%hd %hhd", &h, &hh) == 2 && h == (short)70000 && hh == (signed char)300);
	CHECK(sscanf("-1", "%u", &u) == 1 && u == 4294967295u);
	void *p = 0;
	CHECK(sscanf("0x1234", "%p", &p) == 1 && p == (void *)0x1234);
	size_t z = 0;
	CHECK(sscanf("42", "%zu", &z) == 1 && z == 42);
	/* failures */
	a = 99;
	CHECK(sscanf("abc", "%d", &a) == 0 && a == 99);
	CHECK(sscanf("-", "%d", &a) == 0);
	CHECK(sscanf("", "%d", &a) == EOF);
	CHECK(sscanf("   ", "%d", &a) == EOF);
	CHECK(sscanf("1 x", "%d %d", &a, &b) == 1);
	/* glibc-compatible leniency: "0x" alone reads as 0 */
	CHECK(sscanf("0xg", "%x%n", &u, &n) == 1 && u == 0 && n == 2);
}

static void floats(void)
{
	float f = 0;
	double d = 0;
	long double ld = 0;
	int n = 0;
	CHECK(sscanf("3.25", "%f", &f) == 1 && f == 3.25f);
	CHECK(sscanf("-1e-3", "%lf", &d) == 1 && d == -1e-3);
	CHECK(sscanf("0.1", "%Lf", &ld) == 1 && ld == 0.1L);
	CHECK(sscanf("0x1.8p1", "%lf", &d) == 1 && d == 3.0);
	CHECK(sscanf("inf", "%lf", &d) == 1 && d > 1e308);
	CHECK(sscanf("-INFINITY", "%lf", &d) == 1 && d < -1e308);
	CHECK(sscanf("nan", "%lf", &d) == 1 && d != d);
	CHECK(sscanf("infinx", "%lf", &d) == 0);
	CHECK(sscanf("1.5e+", "%lf%n", &d, &n) == 1 && d == 1.5 && n == 5);
	CHECK(sscanf("100ergs", "%lf%n", &d, &n) == 1 && d == 100 && n == 4);
	CHECK(sscanf("1.23456", "%4lf", &d) == 1 && d == 1.23);
	CHECK(sscanf(".", "%lf", &d) == 0);
	CHECK(sscanf("2.5 3.5", "%e %g", &f, &f) == 2 && f == 3.5f);
	/* correctly rounded, via strtod */
	CHECK(sscanf("9007199254740993", "%lf", &d) == 1 && d == 9007199254740992.0);
}

static void strings(void)
{
	char s[16], t[16];
	int n = 0;
	CHECK(sscanf("  hello world", "%s %s", s, t) == 2 && !strcmp(s, "hello") && !strcmp(t, "world"));
	CHECK(sscanf("abcdef", "%3s%s", s, t) == 2 && !strcmp(s, "abc") && !strcmp(t, "def"));
	memset(s, 'x', sizeof s);
	CHECK(sscanf(" ab", "%c", s) == 1 && s[0] == ' ' && s[1] == 'x');
	CHECK(sscanf("abcd", "%3c", s) == 1 && !memcmp(s, "abcx", 4));
	CHECK(sscanf("key=value;rest", "%[a-z]=%[^;]", s, t) == 2 && !strcmp(s, "key") && !strcmp(t, "value"));
	CHECK(sscanf("]]x", "%[]]", s) == 1 && !strcmp(s, "]]"));
	CHECK(sscanf("a-b", "%[a-]", s) == 1 && !strcmp(s, "a-"));
	CHECK(sscanf("123abc", "%*d%s", s) == 1 && !strcmp(s, "abc"));
	CHECK(sscanf("x", "%[0-9]", s) == 0);
	CHECK(sscanf("50%", "%d%%%n", &n, &n) == 1 && n == 3);
	CHECK(sscanf("a:b", "%[^:]:%s", s, t) == 2 && !strcmp(t, "b"));

	/* 'm': allocate */
	char *m = 0, *m2 = 0;
	CHECK(sscanf("dynamic string here", "%ms %m[a-z]", &m, &m2) == 2);
	CHECK(m && !strcmp(m, "dynamic") && m2 && !strcmp(m2, "string"));
	free(m);
	free(m2);
	char *big = malloc(5001);
	memset(big, 'q', 5000);
	big[5000] = 0;
	m = 0;
	CHECK(sscanf(big, "%ms", &m) == 1 && m && strlen(m) == 5000);
	free(m);
	free(big);

	/* wide */
	wchar_t ws[8];
	wchar_t wc = 0;
	CHECK(sscanf("h\xc3\xa9llo", "%ls", ws) == 1 && ws[1] == 0xe9 && ws[4] == 'o' && ws[5] == 0);
	CHECK(sscanf("\xe2\x82\xac", "%lc", &wc) == 1 && wc == 0x20ac);
	CHECK(sscanf("\xff", "%ls", ws) == 0 && errno == EILSEQ);

	/* positional */
	int a = 0, b = 0;
	CHECK(sscanf("1 2", "%2$d %1$d", &a, &b) == 2 && a == 2 && b == 1);
}

static void from_file(void)
{
	int p[2];
	CHECK(pipe(p) == 0);
	CHECK(write(p[1], "10 words 2.5\nnext", 17) == 17);
	close(p[1]);
	FILE *f = fdopen(p[0], "r");
	int a = 0;
	char w[16];
	double d = 0;
	CHECK(fscanf(f, "%d %15s %lf", &a, w, &d) == 3 && a == 10 && !strcmp(w, "words") && d == 2.5);
	/* the terminating newline was pushed back */
	CHECK(fgetc(f) == '\n');
	CHECK(fscanf(f, "%s", w) == 1 && !strcmp(w, "next"));
	CHECK(fscanf(f, "%s", w) == EOF);
	fclose(f);
}

static void mb(void)
{
	wchar_t wc;
	mbstate_t st;
	memset(&st, 0, sizeof st);
	CHECK(mbrtowc(&wc, "A", 1, &st) == 1 && wc == 'A');
	CHECK(mbrtowc(&wc, "\xf0\x9f\x98\x80", 4, &st) == 4 && wc == 0x1f600);
	/* byte at a time */
	CHECK(mbrtowc(&wc, "\xe2", 1, &st) == (size_t)-2 && !mbsinit(&st));
	CHECK(mbrtowc(&wc, "\x82", 1, &st) == (size_t)-2);
	CHECK(mbrtowc(&wc, "\xac", 1, &st) == 1 && wc == 0x20ac && mbsinit(&st));
	/* invalid: overlong, surrogate, too large, stray continuation */
	CHECK(mbrtowc(&wc, "\xc0\x80", 2, &st) == (size_t)-1 && errno == EILSEQ);
	CHECK(mbrtowc(&wc, "\xe0\x80\x80", 3, &st) == (size_t)-1);
	CHECK(mbrtowc(&wc, "\xed\xa0\x80", 3, &st) == (size_t)-1);
	CHECK(mbrtowc(&wc, "\xf4\x90\x80\x80", 4, &st) == (size_t)-1);
	CHECK(mbrtowc(&wc, "\x80", 1, &st) == (size_t)-1);
	char b[8];
	CHECK(wcrtomb(b, 0x10ffff, 0) == 4 && !memcmp(b, "\xf4\x8f\xbf\xbf", 4));
	CHECK(wcrtomb(b, 0xd800, 0) == (size_t)-1);
	CHECK(wctomb(b, L'x') == 1 && mblen("\xc3\xa9", 2) == 2 && mblen("", 1) == 0);
	wchar_t w[8];
	CHECK(mbstowcs(w, "a\xc3\xa9z", 8) == 3 && w[1] == 0xe9 && w[3] == 0);
	CHECK(mbstowcs(0, "a\xc3\xa9z", 0) == 3);
	char o[8];
	CHECK(wcstombs(o, L"a\xe9z", sizeof o) == 4 && !strcmp(o, "a\xc3\xa9z"));
	CHECK(wcstombs(0, L"\x20ac", 0) == 3);
	CHECK(wcslen(L"abc") == 3 && wcscmp(L"abc", L"abd") < 0 && wcschr(L"abc", L'c'));
	wchar_t *d = wcsdup(L"dup");
	CHECK(d && !wcscmp(d, L"dup"));
	free(d);
}

int main(void)
{
	ints();
	floats();
	strings();
	from_file();
	mb();
	return t_done();
}
