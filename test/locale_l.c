/* The POSIX 2008 locale API, mbsnrtowcs and wcsnrtombs. */
#include <ctype.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include "harness.h"

int main(void)
{
	CHECK(LC_ALL_MASK == (LC_CTYPE_MASK | LC_NUMERIC_MASK | LC_TIME_MASK | LC_COLLATE_MASK |
	                      LC_MONETARY_MASK | LC_MESSAGES_MASK));
	locale_t l = newlocale(LC_ALL_MASK, "C.UTF-8", 0);
	CHECK(l != 0);
	errno = 0;
	CHECK(!newlocale(LC_ALL_MASK, "de_DE.ISO-8859-1", 0) && errno == ENOENT && newlocale(LC_ALL_MASK, "de_DE.UTF-8", 0));
	errno = 0;
	CHECK(!newlocale(1 << 12, "C", 0) && errno == EINVAL);
	CHECK(!newlocale(LC_CTYPE_MASK, 0, 0));
	unsetenv("LC_ALL");
	setenv("LANG", "pt_BR.UTF-8", 1);
	CHECK(newlocale(LC_ALL_MASK, "", 0));
	setenv("LC_NUMERIC", "pt_BR.ISO-8859-1", 1);
	CHECK(!newlocale(LC_ALL_MASK, "", 0) && newlocale(LC_CTYPE_MASK, "", 0));
	unsetenv("LC_NUMERIC");
	locale_t d = duplocale(l), g = duplocale(LC_GLOBAL_LOCALE);
	CHECK(d && g);
	CHECK(uselocale(l) == LC_GLOBAL_LOCALE && uselocale(0) == l);
	uselocale(LC_GLOBAL_LOCALE);

	CHECK(isalpha_l('a', l) && !isdigit_l('a', l) && isxdigit_l('F', l) && isspace_l('\t', l));
	CHECK(toupper_l('q', l) == 'Q' && tolower_l('Q', l) == 'q' && isblank_l(' ', l) && ispunct_l('!', l));
	CHECK(iswalpha_l(L'é', l) && iswupper_l(L'É', l) && towupper_l(L'é', l) == L'É');
	CHECK(iswctype_l(L'5', wctype_l("digit", l), l) && towctrans_l(L'a', wctrans_l("toupper", l), l) == L'A');
	CHECK(strcoll_l("a", "b", l) < 0 && strcasecmp_l("AbC", "aBc", l) == 0 && strncasecmp_l("abX", "ABy", 2, l) == 0);
	char x[8];
	CHECK(strxfrm_l(x, "abc", sizeof x, l) == 3 && !strcmp(x, "abc"));
	CHECK(wcscoll_l(L"b", L"a", l) > 0);
	CHECK(!strcmp(strerror_l(ENOENT, l), strerror(ENOENT)));
	CHECK(!strcmp(nl_langinfo_l(CODESET, l), "UTF-8"));
	struct tm tm = { .tm_year = 100, .tm_mon = 0, .tm_mday = 2 };
	char ts[32];
	CHECK(strftime_l(ts, sizeof ts, "%Y-%m-%d", &tm, l) == 10 && !strcmp(ts, "2000-01-02"));
	char *e;
	CHECK(strtod_l("0.1x", &e, l) == 0.1 && *e == 'x' && strtof_l("2.5", 0, l) == 2.5f && strtold_l("3", 0, l) == 3);
	CHECK(strtoll_l("-42", 0, 10, l) == -42 && strtoull_l("ff", 0, 16, l) == 255);
	CHECK(wcscasecmp(L"ÉtÉ", L"éTé") == 0 && wcsncasecmp(L"abX", L"ABy", 2) == 0 && wcscasecmp(L"a", L"B") < 0);

	/* mbsnrtowcs: stops after n bytes, keeping a split character */
	const char *mb = "aéb";   /* 61 c3 a9 62 */
	const char *src = mb;
	mbstate_t st = { 0 };
	wchar_t w[8];
	CHECK(mbsnrtowcs(w, &src, 2, 8, &st) == 1 && w[0] == L'a' && src == mb + 2 && !mbsinit(&st));
	CHECK(mbsnrtowcs(w, &src, 10, 8, &st) == 2 && w[0] == L'é' && w[1] == L'b' && src == 0);
	src = mb;
	CHECK(mbsnrtowcs(0, &src, 3, 0, &(mbstate_t){ 0 }) == 2 && src == mb);
	src = "\xff";
	errno = 0;
	CHECK(mbsnrtowcs(w, &src, 1, 8, &(mbstate_t){ 0 }) == (size_t)-1 && errno == EILSEQ);

	/* wcsnrtombs: at most n wide characters, only whole ones written */
	const wchar_t *ws = L"€ab";
	const wchar_t *wsrc = ws;
	char out[8];
	CHECK(wcsnrtombs(out, &wsrc, 2, sizeof out, &(mbstate_t){ 0 }) == 4 && wsrc == ws + 2 && !memcmp(out, "€a", 4));
	wsrc = ws;
	CHECK(wcsnrtombs(out, &wsrc, 10, 2, &(mbstate_t){ 0 }) == 0 && wsrc == ws);
	wsrc = ws;
	CHECK(wcsnrtombs(out, &wsrc, 10, sizeof out, &(mbstate_t){ 0 }) == 5 && wsrc == 0 && !strcmp(out, "€ab"));
	CHECK(wcsnrtombs(0, &(const wchar_t *){ ws }, 10, 0, &(mbstate_t){ 0 }) == 5);

	freelocale(d);
	freelocale(l);
	return t_done();
}
