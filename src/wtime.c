/* wcsftime over strftime: the format is narrowed to UTF-8, formatted, and
 * the result widened. */
#include <stdlib.h>
#include <time.h>
#include <wchar.h>

size_t wcsftime(wchar_t *__restrict s, size_t max, const wchar_t *__restrict fmt, const struct tm *__restrict tm)
{
	if (!max)
		return 0;
	mbstate_t st = { 0 };
	const wchar_t *src = fmt;
	size_t fl = wcsrtombs(0, &src, 0, &st);
	if (fl == (size_t)-1)
		return 0;
	char *nf = malloc(fl + 1);
	if (!nf)
		return 0;
	src = fmt;
	wcsrtombs(nf, &src, fl + 1, &st);

	/* A wide character is at most 4 bytes of UTF-8, so a result that
	 * fits in max wide characters fits in 4 * max bytes. strftime
	 * returns 0 both for "too small" and for an empty result, so grow
	 * the buffer until it succeeds or reaches that bound. */
	size_t limit = max > ((size_t)1 << 20) / 4 ? (size_t)1 << 20 : 4 * max;
	size_t cap = 256 < limit ? 256 : limit, n = 0;
	char *out = 0;
	for (;;) {
		char *o = realloc(out, cap);
		if (!o)
			break;
		out = o;
		n = strftime(out, cap, nf, tm);
		if (n || cap >= limit)
			break;
		cap = cap * 2 < limit ? cap * 2 : limit;
	}
	free(nf);
	size_t r = 0;
	if (out && n) {
		const char *p = out;
		st = (mbstate_t){ 0 };
		r = mbsrtowcs(s, &p, max, &st);
		if (r == (size_t)-1 || r >= max)
			r = 0;
	}
	if (!r)
		s[0] = 0;
	free(out);
	return r;
}
