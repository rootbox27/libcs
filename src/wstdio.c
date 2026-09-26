/* Wide-character stdio. Streams carry UTF-8, the only multibyte encoding
 * this library has: each wide character read is decoded from the stream's
 * bytes and each one written is encoded into them.
 *
 * Orientation: the first wide operation on an unoriented stream makes it
 * wide, the first byte operation makes it byte-oriented (see __toread and
 * __towrite); fwide reports and sets it. An invalid or truncated sequence
 * on input, or a value that is no character on output, fails with EILSEQ
 * and sets the stream's error indicator. */
#include "stdio_impl.h"
#include <errno.h>
#include <limits.h>
#include <wchar.h>

static void orient(FILE *f)
{
	if (!f->mode)
		f->mode = 1;
}

int fwide(FILE *f, int mode)
{
	FLOCK(f);
	if (mode && !f->mode)
		f->mode = mode > 0 ? 1 : -1;
	int r = f->mode;
	FUNLOCK(f);
	return r;
}

/* ---- input ---- */

hidden wint_t __fgetwc_unlocked(FILE *f)
{
	orient(f);
	int c = getc_unlocked_(f);
	if (c == EOF)
		return WEOF;
	if (c < 0x80)
		return (wint_t)c;
	mbstate_t st = { 0 };
	wchar_t wc;
	char b = (char)c;
	size_t r = mbrtowc(&wc, &b, 1, &st);
	int first = 1;
	while (r == (size_t)-2) {
		c = getc_unlocked_(f);
		if (c == EOF) {
			/* truncated by end of file (a read error keeps F_ERR) */
			errno = EILSEQ;
			f->flags |= F_ERR;
			return WEOF;
		}
		first = 0;
		b = (char)c;
		r = mbrtowc(&wc, &b, 1, &st);
	}
	if (r == (size_t)-1) {
		/* The byte that broke the sequence may begin the next
		 * character: leave it for the next read. It is still in the
		 * buffer just behind rpos. */
		if (!first)
			f->rpos--;
		errno = EILSEQ;
		f->flags |= F_ERR;
		return WEOF;
	}
	return (wint_t)wc;
}

wint_t fgetwc(FILE *f)
{
	FLOCK(f);
	wint_t c = __fgetwc_unlocked(f);
	FUNLOCK(f);
	return c;
}

wint_t getwc(FILE *f) { return fgetwc(f); }
wint_t getwchar(void) { return fgetwc(stdin); }
wint_t fgetwc_unlocked(FILE *f) { return __fgetwc_unlocked(f); }
wint_t getwc_unlocked(FILE *f) { return __fgetwc_unlocked(f); }
wint_t getwchar_unlocked(void) { return __fgetwc_unlocked(stdin); }

wchar_t *fgetws_unlocked(wchar_t *__restrict s, int n, FILE *__restrict f)
{
	if (n <= 0) {
		errno = EINVAL;
		return 0;
	}
	int i = 0;
	while (i < n - 1) {
		wint_t c = __fgetwc_unlocked(f);
		if (c == WEOF) {
			if (!i || (f->flags & F_ERR))
				return 0;
			break;
		}
		s[i++] = (wchar_t)c;
		if (c == L'\n')
			break;
	}
	s[i] = 0;
	return s;
}

wchar_t *fgetws(wchar_t *__restrict s, int n, FILE *__restrict f)
{
	FLOCK(f);
	orient(f);
	wchar_t *r = fgetws_unlocked(s, n, f);
	FUNLOCK(f);
	return r;
}

hidden wint_t __ungetwc_unlocked(wint_t c, FILE *f)
{
	if (c == WEOF)
		return WEOF;
	orient(f);
	char b[MB_LEN_MAX];
	mbstate_t st = { 0 };
	size_t l = wcrtomb(b, (wchar_t)c, &st);
	if (l == (size_t)-1)
		return WEOF;
	if (!f->rpos)
		__toread(f);
	if (!f->rpos || f->rpos < f->buf - UNGET + l)
		return WEOF;
	f->rpos -= l;
	for (size_t i = 0; i < l; i++)
		f->rpos[i] = (unsigned char)b[i];
	f->flags &= ~F_EOF;
	return c;
}

wint_t ungetwc(wint_t c, FILE *f)
{
	FLOCK(f);
	c = __ungetwc_unlocked(c, f);
	FUNLOCK(f);
	return c;
}

/* ---- output ---- */

hidden wint_t __fputwc_unlocked(wchar_t c, FILE *f)
{
	orient(f);
	if ((unsigned)c < 0x80)
		return putc_unlocked_(c, f) == EOF ? WEOF : (wint_t)c;
	char b[MB_LEN_MAX];
	mbstate_t st = { 0 };
	size_t l = wcrtomb(b, c, &st);
	if (l == (size_t)-1) {
		f->flags |= F_ERR;
		return WEOF;
	}
	if (__fwritex((const unsigned char *)b, l, f) != l)
		return WEOF;
	return (wint_t)c;
}

wint_t fputwc(wchar_t c, FILE *f)
{
	FLOCK(f);
	wint_t r = __fputwc_unlocked(c, f);
	FUNLOCK(f);
	return r;
}

wint_t putwc(wchar_t c, FILE *f) { return fputwc(c, f); }
wint_t putwchar(wchar_t c) { return fputwc(c, stdout); }
wint_t fputwc_unlocked(wchar_t c, FILE *f) { return __fputwc_unlocked(c, f); }
wint_t putwc_unlocked(wchar_t c, FILE *f) { return __fputwc_unlocked(c, f); }
wint_t putwchar_unlocked(wchar_t c) { return __fputwc_unlocked(c, stdout); }

int fputws_unlocked(const wchar_t *__restrict s, FILE *__restrict f)
{
	orient(f);
	for (; *s; s++)
		if (__fputwc_unlocked(*s, f) == WEOF)
			return -1;
	return 0;
}

int fputws(const wchar_t *__restrict s, FILE *__restrict f)
{
	FLOCK(f);
	int r = fputws_unlocked(s, f);
	FUNLOCK(f);
	return r;
}
