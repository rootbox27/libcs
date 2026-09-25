/* fnmatch: shell pattern matching.
 *
 * Iterative with single-star backtracking, so matching time is at most
 * pattern length x string length; patterns like "*a*a*a*a*b" cannot cause
 * exponential work. */
#include "internal.h"
#include <ctype.h>
#include <fnmatch.h>
#include <string.h>

static int fold(int c, int flags)
{
	return (flags & FNM_CASEFOLD) ? tolower(c) : c;
}

static int in_class(const char *name, size_t l, int c)
{
	static const struct { const char *n; int (*f)(int); } cls[] = {
		{ "alnum", isalnum }, { "alpha", isalpha }, { "blank", isblank }, { "cntrl", iscntrl },
		{ "digit", isdigit }, { "graph", isgraph }, { "lower", islower }, { "print", isprint },
		{ "punct", ispunct }, { "space", isspace }, { "upper", isupper }, { "xdigit", isxdigit },
	};
	for (size_t i = 0; i < sizeof cls / sizeof *cls; i++)
		if (strlen(cls[i].n) == l && !memcmp(cls[i].n, name, l))
			return cls[i].f(c) != 0;
	return -1;
}

/* Match c against the bracket expression at *pp (just after '[').
 * Returns 1/0 and advances *pp past ']'; -1 if there is no closing ']'
 * (the '[' is then literal); -2 for a malformed class, collating symbol
 * or equivalence class (the pattern then matches nothing, as in glibc). */
static int bracket(const char **pp, int c, int flags)
{
	const char *p = *pp;
	int neg = 0, hit = 0;
	if (*p == '!' || *p == '^') {
		neg = 1;
		p++;
	}
	int lc = fold(c, flags);
	for (int first = 1; first || *p != ']'; first = 0) {
		int lo;
		if (!*p)
			return -1;
		if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
			/* collating symbol or equivalence class: only single
			 * characters exist in this locale */
			char end[3] = { p[1], ']', 0 };
			const char *e = strstr(p + 2, end);
			if (!e)
				return -2;
			if (e - (p + 2) == 1 && fold((unsigned char)p[2], flags) == lc)
				hit = 1;
			p = e + 2;
			continue;
		}
		if (p[0] == '[' && p[1] == ':') {
			const char *e = strstr(p + 2, ":]");
			if (!e)
				return -2;
			int r = in_class(p + 2, (size_t)(e - p - 2), c);
			if (r < 0)
				return -2;
			hit |= r;
			if ((flags & FNM_CASEFOLD) && !r && isalpha(c))
				hit |= in_class(p + 2, (size_t)(e - p - 2), isupper(c) ? tolower(c) : toupper(c)) > 0;
			p = e + 2;
			continue;
		}
		if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
			p++;
		lo = (unsigned char)*p++;
		if (p[0] == '-' && p[1] && p[1] != ']') {
			p++;
			if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
				p++;
			int hi = (unsigned char)*p++;
			if (lo <= c && c <= hi)
				hit = 1;
			else if ((flags & FNM_CASEFOLD) && fold(lo, flags) <= lc && lc <= fold(hi, flags))
				hit = 1;
			else if ((flags & FNM_CASEFOLD) && lo <= toupper(c) && toupper(c) <= hi)
				hit = 1;
		} else if (fold(lo, flags) == lc) {
			hit = 1;
		}
	}
	*pp = p + 1;
	return hit != neg;
}

int fnmatch(const char *pat, const char *str, int flags)
{
	const char *p = pat, *s = str;
	const char *star_p = 0, *star_s = 0;
	int pathname = flags & FNM_PATHNAME;
#define LEADING_DOT(x) ((flags & FNM_PERIOD) && *(x) == '.' && ((x) == str || (pathname && (x)[-1] == '/')))

	for (;;) {
		if (LEADING_DOT(s) && *p != '.' &&
		    !(*p == '\\' && !(flags & FNM_NOESCAPE) && p[1] == '.') && *p != 0)
			goto backtrack;
		switch (*p) {
		case 0:
			if (!*s || ((flags & FNM_LEADING_DIR) && *s == '/'))
				return 0;
			goto backtrack;
		case '*':
			while (*p == '*')
				p++;
			star_p = p;
			star_s = s;
			continue;
		case '?':
			if (!*s || (pathname && *s == '/'))
				goto backtrack;
			p++;
			s++;
			continue;
		case '[': {
			if (!*s || (pathname && *s == '/'))
				goto backtrack;
			const char *q = p + 1;
			int r = bracket(&q, (unsigned char)*s, flags);
			if (r == -2)
				return FNM_NOMATCH;
			if (r < 0)
				goto literal; /* no closing ']': '[' is literal */
			if (!r)
				goto backtrack;
			p = q;
			s++;
			continue;
		}
		case '\\':
			if (!(flags & FNM_NOESCAPE)) {
				/* a trailing lone backslash never matches */
				if (!p[1])
					return FNM_NOMATCH;
				p++;
			}
			/* fall through */
		default:
		literal:
			if (fold((unsigned char)*p, flags) != fold((unsigned char)*s, flags) || !*s)
				goto backtrack;
			p++;
			s++;
			continue;
		}
	backtrack:
		/* let the last '*' swallow one more character */
		if (!star_p || !*star_s || (pathname && *star_s == '/') || LEADING_DOT(star_s))
			return FNM_NOMATCH;
		star_s++;
		p = star_p;
		s = star_s;
	}
#undef LEADING_DOT
}
