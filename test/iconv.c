/* <iconv.h> */
#include <iconv.h>
#include <errno.h>
#include <string.h>
#include "harness.h"

/* convert; returns bytes produced or -errno */
static long conv(const char *to, const char *from, const char *in, size_t n, char *out, size_t osz, size_t *ret)
{
	iconv_t cd = iconv_open(to, from);
	if (cd == (iconv_t)-1)
		return -1000;
	char *ip = (char *)in, *op = out;
	size_t il = n, ol = osz;
	size_t r = iconv(cd, &ip, &il, &op, &ol);
	int e = errno;
	iconv_close(cd);
	if (ret)
		*ret = r;
	return r == (size_t)-1 ? -e : (long)(op - out);
}

int main(void)
{
	char out[64];
	const char *u8 = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
	CHECK(conv("UTF-16LE", "UTF-8", u8, 10, out, 64, 0) == 10);
	CHECK(!memcmp(out, "A\0\xe9\0\xac\x20\x3d\xd8\x00\xde", 10));
	CHECK(conv("UTF-16", "UTF-8", "A", 1, out, 64, 0) == 4 && !memcmp(out, "\xff\xfe" "A\0", 4));
	CHECK(conv("UTF-32BE", "UTF-8", "\xc3\xa9", 2, out, 64, 0) == 4 && !memcmp(out, "\0\0\0\xe9", 4));
	CHECK(conv("UTF-8", "UTF-16", "\xfe\xff\0A", 4, out, 64, 0) == 1 && out[0] == 'A');
	CHECK(conv("ISO-8859-1", "UTF-8", "\xc3\xa9", 2, out, 64, 0) == 1 && (unsigned char)out[0] == 0xe9);
	CHECK(conv("ISO-8859-15", "UTF-8", "\xe2\x82\xac", 3, out, 64, 0) == 1 && (unsigned char)out[0] == 0xa4);
	CHECK(conv("UTF-8", "CP1252", "\x93q\x94", 3, out, 64, 0) == 7 && !memcmp(out, "\xe2\x80\x9cq\xe2\x80\x9d", 7));
	CHECK(conv("ascii", "utf8", "ok", 2, out, 64, 0) == 2);
	/* errors */
	CHECK(conv("ASCII", "UTF-8", u8, 10, out, 64, 0) == -EILSEQ);
	CHECK(conv("UTF-8", "UTF-8", "a\xe2\x82", 3, out, 64, 0) == -EINVAL);
	CHECK(conv("UTF-8", "UTF-8", "a\xff", 2, out, 64, 0) == -EILSEQ);
	CHECK(conv("UTF-16LE", "UTF-8", "abc", 3, out, 3, 0) == -E2BIG);
	CHECK(conv("UTF-8", "UTF-16LE", "\x00\xde", 2, out, 64, 0) == -EILSEQ);
	CHECK(conv("NO-SUCH-CHARSET", "UTF-8", "a", 1, out, 64, 0) == -1000);
	/* //TRANSLIT and //IGNORE */
	size_t r;
	CHECK(conv("ASCII//TRANSLIT", "UTF-8", u8, 10, out, 64, &r) == 7 && r == 3 && !memcmp(out, "A?EUR?", 6));
	CHECK(conv("ASCII//IGNORE", "UTF-8", u8, 10, out, 64, 0) == -EILSEQ);
	/* E2BIG leaves the pointers at a character boundary */
	iconv_t cd = iconv_open("UTF-8", "UTF-16LE");
	char *ip = (char *)"a\0b\0c\0", *op = out;
	size_t il = 6, ol = 2;
	CHECK(iconv(cd, &ip, &il, &op, &ol) == (size_t)-1 && errno == E2BIG && il == 2 && ol == 0);
	ol = 10;
	CHECK(iconv(cd, &ip, &il, &op, &ol) == 0 && !memcmp(out, "abc", 3));
	iconv_close(cd);
	return t_done();
}
