/* <uchar.h>: UTF-16 and UTF-32 conversions over the UTF-8 multibyte
 * functions. A pending surrogate is kept in the mbstate_t: bit 31 of __n
 * marks a low surrogate still to be returned by mbrtoc16, bit 30 a high
 * surrogate given to c16rtomb and waiting for its partner. */
#include <uchar.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>

#define PEND_LOW  0x80000000u
#define PEND_HIGH 0x40000000u

size_t mbrtoc32(char32_t *restrict c, const char *restrict s, size_t n, mbstate_t *restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (!s)
		return mbrtoc32(0, "", 1, st);
	wchar_t wc;
	size_t r = mbrtowc(&wc, s, n, st);
	if (r <= 4 && c)
		*c = (char32_t)wc;
	return r;
}

size_t c32rtomb(char *restrict s, char32_t c, mbstate_t *restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
		errno = EILSEQ;
		return (size_t)-1;
	}
	return wcrtomb(s, (wchar_t)c, st);
}

size_t mbrtoc16(char16_t *restrict c, const char *restrict s, size_t n, mbstate_t *restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (st->__n & PEND_LOW) {
		if (c)
			*c = (char16_t)st->__st;
		st->__n = st->__st = 0;
		return (size_t)-3;
	}
	if (!s)
		return mbrtoc16(0, "", 1, st);
	wchar_t wc;
	size_t r = mbrtowc(&wc, s, n, st);
	if (r > 4)
		return r;
	if ((unsigned)wc >= 0x10000) {
		unsigned v = (unsigned)wc - 0x10000;
		if (c)
			*c = (char16_t)(0xd800 | v >> 10);
		st->__st = 0xdc00 | (v & 0x3ff);
		st->__n = PEND_LOW;
	} else if (c) {
		*c = (char16_t)wc;
	}
	return r;
}

size_t c16rtomb(char *restrict s, char16_t c, mbstate_t *restrict st)
{
	static mbstate_t internal;
	char buf[MB_LEN_MAX];
	if (!st)
		st = &internal;
	if (!s) {
		s = buf;
		c = 0;
	}
	if (st->__n & PEND_HIGH) {
		if (c < 0xdc00 || c > 0xdfff) {
			st->__n = st->__st = 0;
			errno = EILSEQ;
			return (size_t)-1;
		}
		unsigned v = 0x10000 + ((st->__st - 0xd800) << 10) + (c - 0xdc00u);
		st->__n = st->__st = 0;
		return wcrtomb(s, (wchar_t)v, st);
	}
	if (c >= 0xd800 && c <= 0xdbff) {
		st->__st = c;
		st->__n = PEND_HIGH;
		return 0;
	}
	if (c >= 0xdc00 && c <= 0xdfff) {
		errno = EILSEQ;
		return (size_t)-1;
	}
	return wcrtomb(s, (wchar_t)c, st);
}
