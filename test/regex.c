/* <regex.h> */
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

/* match pat against s; expect is "so,eo so,eo ..." or "-" for no match */
static int m(const char *pat, int cf, const char *s, int ef, const char *expect)
{
	regex_t re;
	regmatch_t pm[10];
	char got[256] = "";
	int r = regcomp(&re, pat, cf);
	if (r)
		return 0;
	r = regexec(&re, s, 10, pm, ef);
	if (r) {
		strcpy(got, "-");
	} else {
		char *o = got;
		for (size_t i = 0; i <= re.re_nsub && i < 10; i++) {
			long a = pm[i].rm_so, b = pm[i].rm_eo;
			char tmp[48], *t = tmp + sizeof tmp;
			*--t = 0;
			/* "a,b " without printf */
			long v[2] = { a, b };
			for (int k = 1; k >= 0; k--) {
				long x = v[k] < 0 ? -v[k] : v[k];
				do *--t = (char)('0' + x % 10); while (x /= 10);
				if (v[k] < 0)
					*--t = '-';
				if (k)
					*--t = ',';
			}
			if (i)
				*o++ = ' ';
			strcpy(o, t);
			o += strlen(t);
		}
	}
	regfree(&re);
	if (strcmp(got, expect)) {
		t_puts(pat);
		t_puts(" on ");
		t_puts(s);
		t_puts(": got ");
		t_puts(got);
		t_puts("\n");
		return 0;
	}
	return 1;
}

static int err(const char *pat, int cf, int want)
{
	regex_t re;
	int r = regcomp(&re, pat, cf);
	if (!r)
		regfree(&re);
	return r == want;
}

int main(void)
{
	const int E = REG_EXTENDED;
	/* basics, leftmost-longest */
	CHECK(m("abc", 0, "xabcx", 0, "1,4"));
	CHECK(m("a|ab|abc", E, "abcd", 0, "0,3"));
	CHECK(m("(a|ab)(c|bcd)(d*)", E, "abcd", 0, "0,4 0,1 1,4 4,4"));
	CHECK(m("(a*)(b*)(c*)", E, "aabbbc", 0, "0,6 0,2 2,5 5,6"));
	CHECK(m("(a*)*", E, "", 0, "0,0 0,0"));
	CHECK(m("(a*)+", E, "aaa", 0, "0,3 0,3"));
	CHECK(m("(ab)+", E, "ababx", 0, "0,4 2,4"));
	CHECK(m("(a)|(b)", E, "b", 0, "0,1 -1,-1 0,1"));
	CHECK(m("x=([0-9]+), y=([0-9]+)", E, "x=1, y=22", 0, "0,9 2,3 7,9"));
	CHECK(m("a{2,3}", E, "aaaa", 0, "0,3"));
	CHECK(m("a{,2}", E, "aaa", 0, "0,2"));
	CHECK(m("a\\{2\\}", 0, "aaa", 0, "0,2"));
	/* BRE specials */
	CHECK(m("\\(a\\)\\(b\\)", 0, "ab", 0, "0,2 0,1 1,2"));
	CHECK(m("*a", 0, "x*a", 0, "1,3"));
	CHECK(m("^*", 0, "*x", 0, "0,1"));
	CHECK(m("a^", 0, "a^", 0, "0,2"));
	CHECK(m("$b", 0, "$b", 0, "0,2"));
	CHECK(m("a\\|b", 0, "b", 0, "0,1"));
	CHECK(m("a\\+", 0, "caa", 0, "1,3"));
	/* anchors, newlines, flags */
	CHECK(m("$", E, "abc", 0, "3,3"));
	CHECK(m("^b", E | REG_NEWLINE, "a\nb", 0, "2,3"));
	CHECK(m("^b", E, "a\nb", 0, "-"));
	CHECK(m("a.c", E | REG_NEWLINE, "a\nc", 0, "-"));
	CHECK(m("[^x]*", E | REG_NEWLINE, "ab\ncd", 0, "0,2"));
	CHECK(m("^a", E, "abc", REG_NOTBOL, "-"));
	CHECK(m("c$", E, "abc", REG_NOTEOL, "-"));
	CHECK(m("ABC", E | REG_ICASE, "xabc", 0, "1,4"));
	CHECK(m("[A-C]+", E | REG_ICASE, "abcd", 0, "0,3"));
	/* brackets and classes */
	CHECK(m("[]a]+", E, "]a]b", 0, "0,3"));
	CHECK(m("[a-]+", E, "-a-b", 0, "0,3"));
	CHECK(m("[[:digit:]]+", E, "ab123c", 0, "2,5"));
	CHECK(m("[[.-.]a]+", E, "-a-", 0, "0,3"));
	CHECK(m("[[=a=]b]+", E, "abab", 0, "0,4"));
	CHECK(m("\\w+", E, "  foo_1 bar", 0, "2,7"));
	CHECK(m("\\<b", E, "foo.bar", 0, "4,5"));
	CHECK(m("o\\b", E, "foo bar", 0, "2,3"));
	/* UTF-8 */
	CHECK(m("h.llo", E, "h\xc3\xa9llo", 0, "0,6"));
	CHECK(m("[\xc3\xa9]", E, "h\xc3\xa9", 0, "1,3"));
	CHECK(m("[[:alpha:]]+", E, "\xc3\xa9t\xc3\xa9!", 0, "0,5"));
	/* back-references */
	CHECK(m("\\(.\\)\\1", 0, "abccd", 0, "2,4 2,3"));
	CHECK(m("(a+)b\\1", E, "aaabaa", 0, "1,6 1,3"));
	CHECK(m("(a)\\1", E | REG_ICASE, "aA", 0, "0,2 0,1"));
	/* REG_STARTEND and REG_NOSUB */
	regex_t re;
	regmatch_t pm[2];
	CHECK(regcomp(&re, "b+", E) == 0);
	pm[0].rm_so = 2;
	pm[0].rm_eo = 4;
	CHECK(regexec(&re, "bbbbbb", 1, pm, REG_STARTEND) == 0 && pm[0].rm_so == 2 && pm[0].rm_eo == 4);
	regfree(&re);
	CHECK(regcomp(&re, "(a)(b)", E | REG_NOSUB) == 0 && re.re_nsub == 2);
	CHECK(regexec(&re, "ab", 0, 0, 0) == 0);
	regfree(&re);
	/* errors */
	CHECK(err("[a", E, REG_EBRACK));
	CHECK(err("(a", E, REG_EPAREN));
	CHECK(err("\\(a", 0, REG_EPAREN));
	CHECK(err("a{1", E, REG_EBRACE));
	CHECK(err("a{2,1}", E, REG_BADBR));
	CHECK(err("*a", E, REG_BADRPT));
	CHECK(err("a\\", E, REG_EESCAPE));
	CHECK(err("[[:foo:]]", E, REG_ECTYPE));
	CHECK(err("[b-a]", E, REG_ERANGE));
	CHECK(err("\\1", 0, REG_ESUBREG));
	CHECK(err("(a{1000}){1000}", E, REG_ESPACE));
	char buf[8];
	CHECK(regerror(REG_EPAREN, 0, buf, sizeof buf) > sizeof buf && strlen(buf) == 7);

	/* no catastrophic backtracking without back-references */
	size_t n = 50000;
	char *s = malloc(n + 1);
	memset(s, 'a', n);
	s[n] = 0;
	CHECK(regcomp(&re, "(a|aa)*b", E) == 0);
	CHECK(regexec(&re, s, 0, 0, 0) == REG_NOMATCH);
	regfree(&re);
	/* exponential back-reference search stops with REG_ESPACE */
	s[40] = 0;
	CHECK(regcomp(&re, "^(a*)*\\1b", E) == 0);
	CHECK(regexec(&re, s, 0, 0, 0) == REG_ESPACE);
	regfree(&re);
	free(s);
	return t_done();
}
