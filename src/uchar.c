/* <uchar.h>: UTF-8, UTF-16 and UTF-32 conversions over the UTF-8
 * multibyte functions. Pending state is kept in the mbstate_t:
 *  - bit 31 of __n: a low surrogate still to be returned by mbrtoc16;
 *  - bit 30: a high surrogate given to c16rtomb, waiting for its partner;
 *  - bit 29: bytes of a character still to be returned by mbrtoc8
 *    (count in the low bits of __n, bytes in __st, first byte lowest);
 *  - bit 28: bytes given to c8rtomb so far (count in the low bits of
 *    __n, bytes in __st). */
#include <uchar.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>

#define PEND_LOW  0x80000000u
#define PEND_HIGH 0x40000000u
#define PEND_C8OUT 0x20000000u
#define PEND_C8IN  0x10000000u

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

size_t mbrtoc8(char8_t *restrict c, const char *restrict s, size_t n, mbstate_t *restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (st->__n & PEND_C8OUT) {
		unsigned k = st->__n & 3;
		if (c)
			*c = (char8_t)(st->__st & 0xff);
		st->__st >>= 8;
		st->__n = --k ? PEND_C8OUT | k : 0;
		if (!k)
			st->__st = 0;
		return (size_t)-3;
	}
	if (!s)
		return mbrtoc8(0, "", 1, st);
	wchar_t wc;
	size_t r = mbrtowc(&wc, s, n, st);
	if (r > 4)
		return r;
	char b[4];
	mbstate_t tmp = { 0 };
	size_t len = wcrtomb(b, wc, &tmp);
	if (c)
		*c = (char8_t)b[0];
	if (len > 1) {
		st->__st = 0;
		for (size_t i = len - 1; i >= 1; i--)
			st->__st = st->__st << 8 | (unsigned char)b[i];
		st->__n = PEND_C8OUT | (unsigned)(len - 1);
	}
	return r;
}

size_t c8rtomb(char *restrict s, char8_t c, mbstate_t *restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (!s) {
		/* reset; an incomplete character is an error */
		int bad = (st->__n & PEND_C8IN) != 0;
		st->__n = st->__st = 0;
		if (bad) {
			errno = EILSEQ;
			return (size_t)-1;
		}
		return 1;
	}
	unsigned have = st->__n & PEND_C8IN ? st->__n & 3 : 0;
	if (!have) {
		if (c < 0x80) {
			*s = (char)c;
			return 1;
		}
		if (c < 0xc2 || c > 0xf4) {
			errno = EILSEQ;
			return (size_t)-1;
		}
		st->__st = c;
		st->__n = PEND_C8IN | 1;
		return 0;
	}
	unsigned char b[4];
	for (unsigned i = 0; i < have; i++)
		b[i] = (unsigned char)(st->__st >> (8 * i));
	b[have] = c;
	size_t need = b[0] < 0xe0 ? 2 : b[0] < 0xf0 ? 3 : 4;
	/* check what we have so far: mbrtowc rejects a bad prefix at once */
	mbstate_t tmp = { 0 };
	wchar_t wc;
	size_t r = mbrtowc(&wc, (const char *)b, have + 1, &tmp);
	if (r == (size_t)-1) {
		st->__n = st->__st = 0;
		errno = EILSEQ;
		return (size_t)-1;
	}
	if (have + 1 < need) {
		st->__st |= (unsigned)c << (8 * have);
		st->__n = PEND_C8IN | (have + 1);
		return 0;
	}
	st->__n = st->__st = 0;
	for (size_t i = 0; i < need; i++)
		s[i] = (char)b[i];
	return need;
}
