/* Wide-character stdio: orientation, character I/O, wprintf, wscanf.
 * Expected values were checked against glibc (C.UTF-8); where they differ
 * the comment says why. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include "harness.h"

static wchar_t b[256];

#define P(want, ...) do { \
	int r_ = swprintf(b, 256, __VA_ARGS__); \
	CHECK(r_ == (int)wcslen(want) && !wcscmp(b, want)); \
} while (0)

static void printing(void)
{
	P(L"42|   42|42   |00042|+42| 42|007|ff|0XFF|010|-9000000000",
	  L"%d|%5d|%-5d|%05d|%+d|% d|%.3d|%x|%#X|%#o|%lld", 42, 42, 42, 42, 42, 42, 7, 255, 255, 8, -9000000000LL);
	P(L"3.141590|1.23e+04|0.0001|0x1p+0|    -2.500|2.2       |1.500000",
	  L"%f|%.2e|%g|%a|%10.3f|%-10.1f|%Lf", 3.14159, 12345.678, 0.0001, 1.0, -2.5, 2.25, 1.5L);
	/* widths and precisions count wide characters, not bytes */
	P(L"ab|       été|x     |ét|wé|   yz|€",
	  L"%s|%10s|%-6s|%.2s|%ls|%5ls|%.1ls", "ab", "été", "x", "été", L"wé", L"yz", L"€q");
	P(L"a|é|  z|b  |€", L"%c|%lc|%3lc|%-3c|%C", 'a', L'é', L'z', 'b', L'€');
	P(L"0x1234|%|%", L"%p|%5%|%%", (void *)0x1234);
	P(L"x 5 x", L"%2$s %1$d %2$s", 5, "x");
	P(L"     1|2   |1.000", L"%*d|%-*d|%.*f", 6, 1, 4, 2, 3, 1.0);
	P(L"(null)|(null)|", L"%s|%ls|%.3s", (char *)0, (wchar_t *)0, (char *)0);
	/* a huge exact decimal goes through without a buffer limit */
	int r = swprintf(b, 256, L"%.200f", 1e-10);
	CHECK(r == 202 && !wcsncmp(b, L"0.00000000010000000000000000364321", 34));

	/* output that does not fit is an error, still terminated */
	wchar_t small[5];
	errno = 0;
	CHECK(swprintf(small, 5, L"%s", "hello") == -1 && !wcscmp(small, L"hell"));
	wchar_t six[6];
	CHECK(swprintf(six, 6, L"%s", "hello") == 5);
	CHECK(swprintf(six, 0, L"x") == -1);
	/* invalid multibyte input; %c of a byte that is no character
	 * (glibc writes something; C says btowc, which fails) */
	errno = 0;
	CHECK(swprintf(b, 10, L"%s", "\xff") == -1 && errno == EILSEQ);
	errno = 0;
	CHECK(swprintf(b, 10, L"%c", 0xe9) == -1 && errno == EILSEQ);
	errno = 0;
	CHECK(swprintf(b, 10, L"%1$d %d", 1, 2) == -1 && errno == EINVAL);
}

static void scanning(void)
{
	int a = 0, n = 0;
	long l = 0;
	double d = 0;
	float f = 0;
	unsigned x = 0;
	char s[32] = { 0 };
	wchar_t ws[32] = { 0 };
	int r = swscanf(L"  12 -34 5.5e2 0x1F héllo wörld 7", L"%d %ld %lf %x %s %ls %n%f",
	                &a, &l, &d, &x, s, ws, &n, &f);
	CHECK(r == 7 && a == 12 && l == -34 && d == 550 && x == 31);
	CHECK(!strcmp(s, "héllo") && !wcscmp(ws, L"wörld") && n == 32 && f == 7);
	char c3[8] = { 0 };
	wchar_t set[10] = { 0 };
	/* %c without l stores multibyte: 2 wide chars, 3 bytes */
	CHECK(swscanf(L"éxabcZZ", L"%2c%9l[a-c]", c3, set) == 2 && !strcmp(c3, "éx") && !wcscmp(set, L"abc"));
	CHECK(swscanf(L"abc", L"%d", &a) == 0);
	CHECK(swscanf(L"", L"%d", &a) == EOF);
	CHECK(swscanf(L"1e", L"%lf", &d) == 1 && d == 1);
	CHECK(swscanf(L"0x", L"%i", &a) == 1 && a == 0);
	CHECK(swscanf(L"42%x", L"%d%%x", &a) == 1 && a == 42);
	char *m = 0;
	CHECK(swscanf(L"dyné rest", L"%ms", &m) == 1 && m && !strcmp(m, "dyné"));
	free(m);
	wchar_t *wm = 0;
	CHECK(swscanf(L"€€!", L"%ml[€]", &wm) == 1 && wm && !wcscmp(wm, L"€€"));
	free(wm);
	CHECK(swscanf(L"5 6", L"%2$d %1$d", &a, &n) == 2 && a == 6 && n == 5);
	CHECK(swscanf(L"]x-", L"%9[]x-]", s) == 1 && !strcmp(s, "]x-"));
	CHECK(swscanf(L"q", L"%[^a-z]", s) == 0);
	/* C's %f takes all of strtod's forms, including nan(chars) and hex
	 * (glibc stops at the parenthesis) */
	double e = 0;
	CHECK(swscanf(L"inf nan(abc) 0x1p3", L"%lf %f %lf", &d, &f, &e) == 3 && e == 8);
	/* a long number converts exactly */
	static wchar_t longnum[1100];
	longnum[0] = L'1';
	wmemset(longnum + 1, L'0', 1000);
	wcscpy(longnum + 1001, L"e-1000");
	CHECK(swscanf(longnum, L"%lf", &d) == 1 && d == 1);
}

static void streams(void)
{
	FILE *t = tmpfile();
	CHECK(t && fwide(t, 0) == 0);
	CHECK(fwprintf(t, L"%ls %d\n", L"hé", 5) == 5);
	CHECK(fwide(t, 0) > 0 && fwide(t, -1) > 0); /* once set it stays */
	CHECK(fputws(L"€\U0001f600\n", t) >= 0 && fputwc(L'z', t) == L'z');
	rewind(t);
	wchar_t line[40];
	CHECK(fgetws(line, 40, t) == line && !wcscmp(line, L"hé 5\n"));
	wint_t c = fgetwc(t);
	CHECK(c == 0x20ac && ungetwc(c, t) == c && fgetwc(t) == c);
	CHECK(fgetwc(t) == 0x1f600 && fgetwc(t) == L'\n' && fgetwc(t) == L'z' && fgetwc(t) == WEOF && feof(t));
	/* fgetws stops at n - 1 characters */
	rewind(t);
	CHECK(fgetws(line, 3, t) == line && !wcscmp(line, L"hé"));
	/* fwscanf leaves the character after the match unread */
	rewind(t);
	wchar_t w2[10];
	int v = 0;
	CHECK(fwscanf(t, L"%ls %d", w2, &v) == 2 && !wcscmp(w2, L"hé") && v == 5 && fgetwc(t) == L'\n');
	fclose(t);

	/* invalid input: EILSEQ, error indicator, and the byte that broke
	 * the sequence is read next */
	char path[] = "/tmp/citadel-wideXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	CHECK(write(fd, "a\xc3(b\xff", 5) == 5);
	close(fd);
	FILE *u = fopen(path, "r");
	CHECK(u && fgetwc(u) == L'a');
	errno = 0;
	CHECK(fgetwc(u) == WEOF && errno == EILSEQ && ferror(u));
	clearerr(u);
	CHECK(fgetwc(u) == L'(' && fgetwc(u) == L'b');
	errno = 0;
	CHECK(fgetwc(u) == WEOF && errno == EILSEQ);
	fclose(u);
	/* a sequence cut off by the end of the file */
	u = fopen(path, "w");
	fputs("\xe2\x82", u);
	fclose(u);
	u = fopen(path, "r");
	errno = 0;
	CHECK(fgetwc(u) == WEOF && errno == EILSEQ);
	fclose(u);
	/* output of a value that is no character */
	u = fopen(path, "w");
	CHECK(fputwc((wchar_t)0xd800, u) == WEOF && ferror(u));
	CHECK(fputwc((wchar_t)0x110000, u) == WEOF);
	fclose(u);
	unlink(path);

	/* byte functions orient a stream the other way */
	FILE *by = tmpfile();
	fputs("x", by);
	CHECK(fwide(by, 0) < 0 && fwide(by, 1) < 0);
	fclose(by);
	FILE *w = tmpfile();
	CHECK(fwide(w, 1) > 0);
	fclose(w);
}

int main(void)
{
	printing();
	scanning();
	streams();
	/* and to a real unbuffered stream */
	CHECK(fwprintf(stderr, L"%ls", L"") == 0);
	return t_done();
}
