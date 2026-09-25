/* Multibyte conversion. The only character encoding is UTF-8; decoding
 * rejects overlong forms, surrogates and values above U+10FFFF.
 *
 * mbstate_t holds a partially decoded character: __st is the code point
 * so far, __n the number of continuation bytes still needed, and the
 * top bits of __n the lower bound on the next byte for the second byte of
 * a sequence (to reject overlongs and surrogates incrementally). */
#include "internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ILSEQ ((size_t)-1)
#define INCOMPLETE ((size_t)-2)

/* Bounds for the second byte of a sequence, indexed by lead byte class. */
static int second_ok(unsigned lead, unsigned b)
{
	if (lead == 0xe0)
		return b >= 0xa0 && b <= 0xbf; /* no overlong 3-byte */
	if (lead == 0xed)
		return b >= 0x80 && b <= 0x9f; /* no surrogates */
	if (lead == 0xf0)
		return b >= 0x90 && b <= 0xbf; /* no overlong 4-byte */
	if (lead == 0xf4)
		return b >= 0x80 && b <= 0x8f; /* <= U+10FFFF */
	return b >= 0x80 && b <= 0xbf;
}

size_t mbrtowc(wchar_t *__restrict wc, const char *__restrict src, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	if (!src) {
		/* reset; an unfinished sequence is an error */
		int bad = st->__n != 0;
		st->__st = st->__n = 0;
		if (bad) {
			errno = EILSEQ;
			return ILSEQ;
		}
		return 0;
	}
	if (!n)
		return INCOMPLETE;
	const unsigned char *s = (const unsigned char *)src;
	unsigned c = st->__st;
	unsigned need = st->__n & 7;
	unsigned lead = st->__n >> 8; /* lead byte, while the second is pending */
	size_t i = 0;
	if (!need) {
		unsigned b = s[i++];
		if (b < 0x80) {
			if (wc)
				*wc = (wchar_t)b;
			return b ? 1 : 0;
		}
		if (b >= 0xc2 && b <= 0xdf) {
			c = b & 0x1f;
			need = 1;
		} else if (b >= 0xe0 && b <= 0xef) {
			c = b & 0x0f;
			need = 2;
		} else if (b >= 0xf0 && b <= 0xf4) {
			c = b & 0x07;
			need = 3;
		} else {
			goto ilseq;
		}
		lead = b;
	}
	while (need) {
		if (i == n) {
			st->__st = c;
			st->__n = need | lead << 8;
			return INCOMPLETE;
		}
		unsigned b = s[i++];
		if (lead ? !second_ok(lead, b) : (b & 0xc0) != 0x80)
			goto ilseq;
		lead = 0;
		c = c << 6 | (b & 0x3f);
		need--;
	}
	st->__st = st->__n = 0;
	if (wc)
		*wc = (wchar_t)c;
	return i;
ilseq:
	st->__st = st->__n = 0;
	errno = EILSEQ;
	return ILSEQ;
}

size_t mbrlen(const char *__restrict s, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	return mbrtowc(0, s, n, st ? st : &internal);
}

int mbsinit(const mbstate_t *st)
{
	return !st || !st->__n;
}

size_t wcrtomb(char *__restrict s, wchar_t wc, mbstate_t *__restrict st)
{
	char buf[4];
	if (!s) {
		s = buf;
		wc = 0;
	}
	unsigned c = (unsigned)wc;
	if (c < 0x80) {
		s[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		s[0] = (char)(0xc0 | c >> 6);
		s[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (c < 0x10000) {
		if (c - 0xd800 < 0x800)
			goto ilseq;
		s[0] = (char)(0xe0 | c >> 12);
		s[1] = (char)(0x80 | (c >> 6 & 0x3f));
		s[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if (c < 0x110000) {
		s[0] = (char)(0xf0 | c >> 18);
		s[1] = (char)(0x80 | (c >> 12 & 0x3f));
		s[2] = (char)(0x80 | (c >> 6 & 0x3f));
		s[3] = (char)(0x80 | (c & 0x3f));
		return 4;
	}
ilseq:
	errno = EILSEQ;
	return ILSEQ;
}

int mbtowc(wchar_t *__restrict wc, const char *__restrict s, size_t n)
{
	if (!s)
		return 0; /* UTF-8 has no shift states */
	mbstate_t st = { 0, 0 };
	size_t r = mbrtowc(wc, s, n, &st);
	if (r == INCOMPLETE) {
		errno = EILSEQ;
		return -1;
	}
	return r == ILSEQ ? -1 : (int)r;
}

int mblen(const char *s, size_t n)
{
	return mbtowc(0, s, n);
}

int wctomb(char *s, wchar_t wc)
{
	if (!s)
		return 0;
	size_t r = wcrtomb(s, wc, 0);
	return r == ILSEQ ? -1 : (int)r;
}

wint_t btowc(int c)
{
	return c >= 0 && c < 0x80 ? (wint_t)c : WEOF;
}

int wctob(wint_t c)
{
	return c < 0x80 ? (int)c : EOF;
}

size_t mbsrtowcs(wchar_t *__restrict ws, const char **__restrict src, size_t n, mbstate_t *__restrict st)
{
	static mbstate_t internal;
	if (!st)
		st = &internal;
	const char *s = *src;
	size_t cnt = 0;
	for (; !ws || cnt < n; cnt++) {
		wchar_t c;
		size_t r = mbrtowc(&c, s, 4, st);
		if (r == ILSEQ || r == INCOMPLETE) {
			if (ws)
				*src = s;
			if (r == INCOMPLETE)
				errno = EILSEQ;
			return ILSEQ;
		}
		if (ws)
			ws[cnt] = c;
		if (!c) {
			if (ws)
				*src = 0;
			return cnt;
		}
		s += r;
	}
	*src = s;
	return cnt;
}

size_t wcsrtombs(char *__restrict s, const wchar_t **__restrict src, size_t n, mbstate_t *__restrict st)
{
	const wchar_t *w = *src;
	size_t cnt = 0;
	char buf[4];
	for (;; w++) {
		size_t r = wcrtomb(buf, *w, 0);
		if (r == ILSEQ) {
			if (s)
				*src = w;
			return ILSEQ;
		}
		if (s && cnt + r > n) {
			*src = w;
			return cnt;
		}
		if (!*w) {
			if (s) {
				s[cnt] = 0;
				*src = 0;
			}
			return cnt;
		}
		if (s)
			memcpy(s + cnt, buf, r);
		cnt += r;
	}
}

size_t mbstowcs(wchar_t *__restrict ws, const char *__restrict s, size_t n)
{
	mbstate_t st = { 0, 0 };
	const char *p = s;
	return mbsrtowcs(ws, &p, n, &st);
}

size_t wcstombs(char *__restrict s, const wchar_t *__restrict ws, size_t n)
{
	const wchar_t *p = ws;
	return wcsrtombs(s, &p, n, 0);
}
