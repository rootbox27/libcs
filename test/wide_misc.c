/* Wide strings and conversions, wctrans, wcsftime, tmpnam, char8_t. */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>
#include "harness.h"

static void strings(void)
{
	CHECK(wcsspn(L"abcde", L"cba") == 3 && wcscspn(L"abcde", L"ed") == 3);
	const wchar_t *h = L"hello";
	CHECK(wcspbrk(h, L"lo") == h + 2 && !wcspbrk(h, L"xyz"));
	wchar_t buf[32] = L"ab";
	CHECK(wcsncat(buf, L"cdef", 2) == buf && !wcscmp(buf, L"abcd"));
	wchar_t t[] = L"  one,two,,three ";
	wchar_t *save, *tok = wcstok(t, L" ,", &save);
	CHECK(tok && !wcscmp(tok, L"one"));
	CHECK((tok = wcstok(0, L" ,", &save)) && !wcscmp(tok, L"two"));
	CHECK((tok = wcstok(0, L" ,", &save)) && !wcscmp(tok, L"three"));
	CHECK(!wcstok(0, L" ,", &save));
	const wchar_t *hay = L"abcabcabd";
	CHECK(wcsstr(hay, L"abcabd") == hay + 3 && wcsstr(hay, L"") == hay && !wcsstr(hay, L"abd!"));
	/* a needle longer than the stack table, and a worst case for naive search */
	static wchar_t big[20000], nd[300];
	wmemset(big, L'a', 19999);
	wmemset(nd, L'a', 298);
	nd[298] = L'b';
	CHECK(!wcsstr(big, nd));
	big[19998] = L'b';
	CHECK(wcsstr(big, nd) == big + 19998 - 298);
	CHECK(wcscoll(L"a", L"b") < 0 && wcscoll(L"b", L"b") == 0);
	wchar_t x[8];
	CHECK(wcsxfrm(x, L"abc", 8) == 3 && !wcscmp(x, L"abc") && wcsxfrm(0, L"abcd", 0) == 4);
}

static void numbers(void)
{
	wchar_t *e;
	CHECK(wcstol(L"　 -123xyz", &e, 10) == -123 && *e == L'x');
	CHECK(wcstoll(L"9223372036854775807", 0, 10) == LLONG_MAX);
	errno = 0;
	CHECK(wcstoull(L"18446744073709551616", 0, 10) == ULLONG_MAX && errno == ERANGE);
	CHECK(wcstoimax(L"-0x10", &e, 0) == -16 && !*e);
	CHECK(wcstoumax(L"777", 0, 8) == 0777);
	const wchar_t *s = L"   nothing";
	CHECK(wcstol(s, &e, 10) == 0 && e == s);
	CHECK(wcstod(s, &e) == 0 && e == s);
	/* a number stops at the first non-ASCII character */
	CHECK(wcstod(L"1.5²", &e) == 1.5 && *e == L'²');
	/* long digit strings convert exactly as strtod does */
	static char n8[4000];
	static wchar_t w8[4000];
	strcpy(n8, "0.");
	for (int i = 0; i < 3000; i++)
		n8[2 + i] = "0123456789"[(i * 7 + 3) % 10];
	strcat(n8, "e-5");
	for (size_t i = 0; i <= strlen(n8); i++)
		w8[i] = (unsigned char)n8[i];
	CHECK(wcstod(w8, &e) == strtod(n8, 0) && !*e);
	CHECK(wcstold(w8, 0) == strtold(n8, 0) && wcstof(w8, 0) == strtof(n8, 0));
	/* 300 leading zeros are not a range error */
	static wchar_t z[400];
	wmemset(z, L'0', 300);
	z[300] = L'7';
	errno = 0;
	CHECK(wcstol(z, &e, 10) == 7 && errno == 0 && e == z + 301);
	CHECK(wcstod(L"0x1p-3", 0) == 0.125 && isinf(wcstod(L"-inf", 0)));
}

static void trans(void)
{
	wctrans_t lo = wctrans("tolower"), up = wctrans("toupper");
	CHECK(lo && up && lo != up && !wctrans("rot13"));
	CHECK(towctrans(L'Ä', lo) == L'ä' && towctrans(L'ß', up) == L'ß' && towctrans(L'x', up) == L'X');
	CHECK(towctrans(L'x', 0) == L'x');
}

static void ftime(void)
{
	struct tm tm = { .tm_year = 124, .tm_mon = 1, .tm_mday = 29, .tm_hour = 13, .tm_min = 5 };
	wchar_t b[64];
	CHECK(wcsftime(b, 64, L"%Y-%m-%d %H:%M été", &tm) == 20 && !wcscmp(b, L"2024-02-29 13:05 été"));
	CHECK(wcsftime(b, 5, L"%Y-%m-%d", &tm) == 0);
	CHECK(wcsftime(b, 11, L"%Y-%m-%d", &tm) == 10);
}

static void names(void)
{
	char a[L_tmpnam];
	char *p = tmpnam(a), *q = tmpnam(0);
	CHECK(p == a && q && strcmp(p, q) && !strncmp(p, "/tmp/", 5));
	struct stat st;
	CHECK(lstat(p, &st) < 0);
}

static void c8(void)
{
	const char *in = "aé€\U0001f600";
	char8_t out[16];
	size_t n = 0;
	mbstate_t st = { 0 };
	const char *s = in;
	size_t left = strlen(in) + 1;
	for (;;) {
		size_t r = mbrtoc8(&out[n], s, left, &st);
		if (r == 0)
			break;
		CHECK(r != (size_t)-1 && r != (size_t)-2);
		n++;
		if (r != (size_t)-3) {
			s += r;
			left -= r;
		}
	}
	CHECK(n == strlen(in) && !memcmp(out, in, n));
	/* and back, byte by byte */
	char back[16];
	size_t m = 0;
	mbstate_t st2 = { 0 };
	for (size_t i = 0; i < n; i++) {
		size_t r = c8rtomb(back + m, out[i], &st2);
		CHECK(r != (size_t)-1);
		m += r;
	}
	CHECK(m == n && !memcmp(back, in, n));
	/* bad bytes */
	mbstate_t st3 = { 0 };
	char tmp[4];
	errno = 0;
	CHECK(c8rtomb(tmp, 0xc0, &st3) == (size_t)-1 && errno == EILSEQ);
	CHECK(c8rtomb(tmp, 0xe2, &st3) == 0 && c8rtomb(tmp, 'x', &st3) == (size_t)-1);
}

int main(void)
{
	strings();
	numbers();
	trans();
	ftime();
	names();
	c8();
	return t_done();
}
