#include "harness.h"
/* The tests truncate on purpose. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void test_mem(void)
{
	char b[64];
	for (int i = 0; i < 64; i++)
		b[i] = (char)i;
	/* overlapping moves in both directions, all small offsets/lengths */
	for (int off = 0; off < 8; off++)
		for (int n = 0; n < 40; n++) {
			char x[64], y[64];
			memcpy(x, b, 64);
			memcpy(y, b, 64);
			memmove(x + off, x, n);
			for (int i = n - 1; i >= 0; i--)
				y[off + i] = y[i];
			CHECK(memcmp(x, y, 64) == 0);
			memcpy(x, b, 64);
			memcpy(y, b, 64);
			memmove(x, x + off, n);
			for (int i = 0; i < n; i++)
				y[i] = y[off + i];
			CHECK(memcmp(x, y, 64) == 0);
		}
	memset(b, 0xab, 10);
	CHECK((unsigned char)b[9] == 0xab && b[10] == 10);
	CHECK(SGN(memcmp("\x01", "\xff", 1)) < 0);
	CHECK(memcmp("abc", "abd", 2) == 0);
	for (int i = 0; i < 64; i++)
		b[i] = (char)i;
	for (int i = 0; i < 64; i++)
		for (int s = 0; s < 8; s++) {
			char *p = memchr(b + s, i, 64 - s);
			CHECK(i < s ? !p : p == b + i);
		}
	CHECK(memchr(b, 5, 5) == 0);
	const char *h = "xxabcabd";
	CHECK(memrchr(h, 'b', 8) == h + 6);
	CHECK(memmem(h, 8, "abd", 3) == h + 5);
	CHECK(memmem(h, 8, "abe", 3) == 0);
	CHECK(memmem(h, 8, "", 0) == h);
	CHECK(memmem(h, 2, "xxa", 3) == 0);
	char d[8];
	CHECK(memccpy(d, "hello", 'l', 5) == d + 3);
	CHECK(mempcpy(d, "abc", 3) == d + 3);
}

static void test_str(void)
{
	/* strlen and strchr across every alignment */
	char buf[80];
	for (int a = 0; a < 16; a++)
		for (int n = 0; n < 40; n++) {
			memset(buf, 'x', sizeof buf);
			buf[a + n] = 0;
			CHECK(strlen(buf + a) == (size_t)n);
			CHECK(strnlen(buf + a, 5) == (size_t)(n < 5 ? n : 5));
			buf[a + n] = 'y';
			buf[a + n + 1] = 0;
			CHECK(strchr(buf + a, 'y') == buf + a + n);
			CHECK(strchr(buf + a, 'z') == 0);
			CHECK(strchr(buf + a, 0) == buf + a + n + 1);
			CHECK(strchrnul(buf + a, 'z') == buf + a + n + 1);
		}
	const char *r = "abcabc";
	CHECK(strrchr(r, 'c') == r + 5);
	CHECK(strrchr(r, 'x') == 0);
	CHECK(strrchr(r, 0) == r + 6);

	CHECK(SGN(strcmp("a", "b")) == -1);
	CHECK(SGN(strcmp("b", "a")) == 1);
	CHECK(strcmp("abc", "abc") == 0);
	CHECK(SGN(strcmp("ab", "abc")) == -1);
	CHECK(SGN(strcmp("\xff", "a")) == 1); /* compared as unsigned char */
	CHECK(strncmp("abcx", "abcy", 3) == 0);
	CHECK(SGN(strncmp("abcx", "abcy", 4)) == -1);
	CHECK(strncmp("a", "b", 0) == 0);
	CHECK(strcasecmp("HeLLo", "hello") == 0);
	CHECK(SGN(strcasecmp("abc", "ABD")) == -1);
	CHECK(strncasecmp("ABCx", "abcy", 3) == 0);

	char d[32];
	CHECK(strcpy(d, "hello") == d && strcmp(d, "hello") == 0);
	CHECK(stpcpy(d, "hi") == d + 2);
	CHECK(strcat(d, " there") == d && strcmp(d, "hi there") == 0);
	strcpy(d, "ab");
	strncat(d, "cdef", 2);
	CHECK_STR(d, "abcd");
	memset(d, 'x', sizeof d);
	strncpy(d, "ab", 5);
	CHECK(d[0] == 'a' && d[2] == 0 && d[4] == 0 && d[5] == 'x');
	CHECK(stpncpy(d, "abcdef", 3) == d + 3);

	CHECK(strspn("aabbc", "ab") == 4);
	CHECK(strspn("aaab", "a") == 3);
	CHECK(strspn("abc", "") == 0);
	CHECK(strcspn("abcd", "dc") == 2);
	CHECK(strcspn("abcd", "x") == 4);
	CHECK(strcspn("abcd", "") == 4);
	CHECK(strpbrk("abcd", "dc") != 0 && *strpbrk("abcd", "dc") == 'c');
	CHECK(strpbrk("abcd", "xy") == 0);
	const char *hay = "the quick brown fox";
	CHECK(strstr(hay, "brown") == hay + 10);
	CHECK(strstr(hay, "") == hay);
	CHECK(strstr(hay, "browne") == 0);
	CHECK(strcasestr(hay, "QUICK") == hay + 4);
	CHECK(strcasestr(hay, "slow") == 0);
}

static void test_bounded(void)
{
	char d[8];
	CHECK(strlcpy(d, "hello world", sizeof d) == 11);
	CHECK_STR(d, "hello w");
	CHECK(strlcpy(d, "hi", sizeof d) == 2);
	CHECK_STR(d, "hi");
	d[0] = 'q';
	CHECK(strlcpy(d, "abc", 0) == 3 && d[0] == 'q');
	strcpy(d, "ab");
	CHECK(strlcat(d, "cdefghij", sizeof d) == 10);
	CHECK_STR(d, "abcdefg");
	/* not NUL-terminated within size: returns size + strlen(src) */
	memset(d, 'x', sizeof d);
	CHECK(strlcat(d, "abc", sizeof d) == 11);
	CHECK(d[7] == 'x');
}

static void test_tok(void)
{
	char s[] = "  a,b ,, c ";
	char *p;
	CHECK_STR(strtok(s, " ,"), "a");
	CHECK_STR(strtok(0, " ,"), "b");
	CHECK_STR(strtok(0, " ,"), "c");
	CHECK(strtok(0, " ,") == 0);
	CHECK(strtok(0, " ,") == 0);

	char s2[] = "a:b::c";
	char *q = s2;
	CHECK_STR(strsep(&q, ":"), "a");
	CHECK_STR(strsep(&q, ":"), "b");
	CHECK_STR(strsep(&q, ":"), "");
	CHECK_STR(strsep(&q, ":"), "c");
	CHECK(q == 0 && strsep(&q, ":") == 0);

	char s3[] = "x y";
	CHECK_STR(strtok_r(s3, " ", &p), "x");
	CHECK_STR(strtok_r(0, " ", &p), "y");
	CHECK(strtok_r(0, " ", &p) == 0);
}

static void test_misc(void)
{
	CHECK(SGN(strverscmp("a1", "a2")) == -1);
	CHECK(SGN(strverscmp("a9", "a10")) == -1);
	CHECK(SGN(strverscmp("a10", "a9")) == 1);
	CHECK(SGN(strverscmp("000", "00")) == -1);
	CHECK(SGN(strverscmp("00", "01")) == -1);
	CHECK(SGN(strverscmp("01", "010")) == -1);
	CHECK(SGN(strverscmp("010", "09")) == -1);
	CHECK(SGN(strverscmp("09", "0")) == -1);
	CHECK(SGN(strverscmp("0", "1")) == -1);
	CHECK(strverscmp("abc", "abc") == 0);

	CHECK(timingsafe_bcmp("abc", "abc", 3) == 0);
	CHECK(timingsafe_bcmp("abc", "abd", 3) != 0);
	CHECK(timingsafe_bcmp("abc", "xyz", 0) == 0);
	CHECK(timingsafe_memcmp("abc", "abc", 3) == 0);
	CHECK(timingsafe_memcmp("abc", "abd", 3) < 0);
	CHECK(timingsafe_memcmp("abd", "abc", 3) > 0);
	CHECK(timingsafe_memcmp("\x01\xff", "\x02\x00", 2) < 0);
	CHECK(timingsafe_memcmp("\xff", "\x00", 1) > 0);
	for (int a = 0; a < 256; a += 17)
		for (int b = 0; b < 256; b += 13) {
			unsigned char x = (unsigned char)a, y = (unsigned char)b;
			CHECK(SGN(timingsafe_memcmp(&x, &y, 1)) == SGN(memcmp(&x, &y, 1)));
		}

	char z[16];
	memset(z, 'x', sizeof z);
	explicit_bzero(z, 8);
	CHECK(z[0] == 0 && z[7] == 0 && z[8] == 'x');

	CHECK(ffs(0) == 0 && ffs(1) == 1 && ffs(0x80) == 8);
	CHECK(ffsl(1L << 40) == 41);

	char *d = strdup("dup me");
	CHECK(d && strcmp(d, "dup me") == 0);
	free(d);
	d = strndup("truncate", 5);
	CHECK(d && strcmp(d, "trunc") == 0);
	free(d);
}

int main(void)
{
	test_mem();
	test_str();
	test_bounded();
	test_tok();
	test_misc();
	return t_done();
}
