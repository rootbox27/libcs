/* glob: pathname pattern expansion, with GNU GLOB_BRACE, GLOB_TILDE,
 * GLOB_ONLYDIR, GLOB_PERIOD and GLOB_NOMAGIC. Components without
 * wildcards are not listed, only checked for existence at the end. */
#include <glob.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct list {
	char **v;
	size_t n, cap;
};

struct ctx {
	int flags;
	int (*errfunc)(const char *, int);
	struct list *l;
	int abort, nomem;
	char path[PATH_MAX];
};

static int add(struct ctx *c, const char *s, int mark)
{
	struct list *l = c->l;
	if (l->n == l->cap) {
		size_t cap = l->cap ? 2 * l->cap : 16;
		char **v = realloc(l->v, cap * sizeof *v);
		if (!v) {
			c->nomem = 1;
			return -1;
		}
		l->v = v;
		l->cap = cap;
	}
	size_t n = strlen(s);
	char *p = malloc(n + 2);
	if (!p) {
		c->nomem = 1;
		return -1;
	}
	memcpy(p, s, n);
	if (mark && (!n || p[n - 1] != '/'))
		p[n++] = '/';
	p[n] = 0;
	l->v[l->n++] = p;
	return 0;
}

static int has_magic(const char *s, size_t n, int noescape)
{
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' && !noescape && i + 1 < n)
			i++;
		else if (s[i] == '*' || s[i] == '?' || s[i] == '[')
			return 1;
	}
	return 0;
}

static int is_dir(const char *p, int follow)
{
	struct stat st;
	return (follow ? stat(p, &st) : lstat(p, &st)) == 0 && S_ISDIR(st.st_mode);
}

/* Expand pat (the rest of the pattern) below c->path[0..plen). */
static void expand(struct ctx *c, size_t plen, const char *pat)
{
	int noesc = c->flags & GLOB_NOESCAPE;
	/* copy literal components straight into the path */
	for (;;) {
		while (*pat == '/') {
			if (plen + 1 >= PATH_MAX)
				return;
			c->path[plen++] = '/';
			pat++;
		}
		if (!*pat) {
			/* the whole pattern was literal from here: does it exist? */
			c->path[plen] = 0;
			struct stat st;
			if (plen && lstat(c->path, &st) == 0) {
				/* GLOB_ONLYDIR is only a hint; literal names are kept */
				int dir = S_ISDIR(st.st_mode) || ((c->flags & GLOB_MARK) && is_dir(c->path, 1));
				add(c, c->path, dir && (c->flags & GLOB_MARK));
			}
			return;
		}
		size_t clen = strcspn(pat, "/");
		if (has_magic(pat, clen, noesc))
			break;
		/* literal: unescape */
		for (size_t i = 0; i < clen; i++) {
			char ch = pat[i];
			if (ch == '\\' && !noesc && i + 1 < clen)
				ch = pat[++i];
			if (plen + 1 >= PATH_MAX)
				return;
			c->path[plen++] = ch;
		}
		pat += clen;
	}

	size_t clen = strcspn(pat, "/");
	char comp[NAME_MAX + 1];
	if (clen > NAME_MAX)
		return;
	memcpy(comp, pat, clen);
	comp[clen] = 0;
	const char *rest = pat + clen;
	int last = !*rest;

	c->path[plen] = 0;
	DIR *d = opendir(plen ? c->path : ".");
	if (!d) {
		if (errno != ENOENT && errno != ENOTDIR) {
			if ((c->errfunc && c->errfunc(plen ? c->path : ".", errno)) || (c->flags & GLOB_ERR))
				c->abort = 1;
		}
		return;
	}
	int fnf = (noesc ? FNM_NOESCAPE : 0) | ((c->flags & GLOB_PERIOD) ? 0 : FNM_PERIOD);
	struct dirent *e;
	while (!c->abort && !c->nomem && (e = readdir(d))) {
		if (fnmatch(comp, e->d_name, fnf))
			continue;
		/* never climb through . or .. unless the pattern says so */
		if (!last && comp[0] != '.' && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")))
			continue;
		size_t nl = strlen(e->d_name);
		if (plen + nl + 1 >= PATH_MAX)
			continue;
		memcpy(c->path + plen, e->d_name, nl + 1);
		if (last) {
			int dir = e->d_type == DT_DIR;
			if ((e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) && (c->flags & (GLOB_MARK | GLOB_ONLYDIR)))
				dir = is_dir(c->path, 1);
			if (!(c->flags & GLOB_ONLYDIR) || dir)
				add(c, c->path, dir && (c->flags & GLOB_MARK));
		} else {
			if (e->d_type != DT_DIR && e->d_type != DT_LNK && e->d_type != DT_UNKNOWN)
				continue;
			if (e->d_type != DT_DIR && !is_dir(c->path, 1))
				continue;
			expand(c, plen + nl, rest);
		}
	}
	closedir(d);
}

/* ~ and ~user; returns a malloced pattern, 0 to use it unchanged, or
 * (char *)-1 for an unknown user with GLOB_TILDE_CHECK */
static char *tilde(const char *pat, int flags)
{
	if (pat[0] != '~' || !(flags & (GLOB_TILDE | GLOB_TILDE_CHECK)))
		return 0;
	size_t ul = strcspn(pat + 1, "/");
	const char *home = 0;
	char user[LOGIN_NAME_MAX + 1];
	struct passwd *pw;
	if (!ul) {
		home = getenv("HOME");
		if (!home && (pw = getpwuid(getuid())))
			home = pw->pw_dir;
	} else if (ul < sizeof user) {
		memcpy(user, pat + 1, ul);
		user[ul] = 0;
		if ((pw = getpwnam(user)))
			home = pw->pw_dir;
	}
	if (!home)
		return (flags & GLOB_TILDE_CHECK) ? (char *)-1 : 0;
	size_t hl = strlen(home), rl = strlen(pat + 1 + ul);
	char *r = malloc(hl + rl + 1);
	if (r) {
		memcpy(r, home, hl);
		memcpy(r + hl, pat + 1 + ul, rl + 1);
	}
	return r;
}

static int cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int glob_one(struct ctx *c, const char *pat)
{
	char *t = tilde(pat, c->flags);
	if (t == (char *)-1)
		return 0;
	size_t first = c->l->n;
	expand(c, 0, t ? t : pat);
	free(t);
	/* sorted per brace alternative, which keep their order */
	if (!(c->flags & GLOB_NOSORT) && c->l->n > first)
		qsort(c->l->v + first, c->l->n - first, sizeof *c->l->v, cmp);
	return c->abort ? GLOB_ABORTED : c->nomem ? GLOB_NOSPACE : 0;
}

/* GLOB_BRACE: expand the first {a,b,...} and recurse */
static int braces(struct ctx *c, const char *pat, int *budget)
{
	int noesc = c->flags & GLOB_NOESCAPE;
	const char *open = 0;
	for (const char *p = pat; *p; p++) {
		if (*p == '\\' && !noesc && p[1]) {
			p++;
			continue;
		}
		if (*p != '{')
			continue;
		/* find the matching close brace and check for a top-level comma */
		int depth = 0, comma = 0;
		const char *q;
		for (q = p; *q; q++) {
			if (*q == '\\' && !noesc && q[1]) {
				q++;
				continue;
			}
			if (*q == '{')
				depth++;
			else if (*q == '}' && !--depth)
				break;
			else if (*q == ',' && depth == 1)
				comma = 1;
		}
		if (*q && comma) {
			open = p;
			break;
		}
	}
	if (!open)
		return glob_one(c, pat);
	if (--*budget < 0)
		return GLOB_NOSPACE;
	size_t pre = (size_t)(open - pat);
	const char *alt = open + 1;
	int depth = 0;
	for (const char *q = alt;; q++) {
		if (*q == '\\' && !noesc && q[1]) {
			q++;
			continue;
		}
		if (*q == '{') {
			depth++;
			continue;
		}
		if ((*q == ',' && !depth) || (*q == '}' && !depth)) {
			const char *close = q;
			if (*q == ',') { /* find the end of the whole group */
				int d2 = 0;
				for (close = q; *close; close++) {
					if (*close == '\\' && !noesc && close[1]) {
						close++;
						continue;
					}
					if (*close == '{')
						d2++;
					else if (*close == '}' && !d2--)
						break;
				}
			}
			size_t al = (size_t)(q - alt), sl = strlen(close + 1);
			char *np = malloc(pre + al + sl + 1);
			if (!np)
				return GLOB_NOSPACE;
			memcpy(np, pat, pre);
			memcpy(np + pre, alt, al);
			memcpy(np + pre + al, close + 1, sl + 1);
			int r = braces(c, np, budget);
			free(np);
			if (r)
				return r;
			if (*q == '}')
				return 0;
			alt = q + 1;
			continue;
		}
		if (*q == '}')
			depth--;
	}
}

int glob(const char *restrict pat, int flags, int (*errfunc)(const char *, int), glob_t *restrict g)
{
	struct list l = { 0, 0, 0 };
	struct ctx *c = malloc(sizeof *c);
	if (!c)
		return GLOB_NOSPACE;
	c->flags = flags;
	c->errfunc = errfunc;
	c->l = &l;
	c->abort = c->nomem = 0;
	if (!(flags & GLOB_APPEND)) {
		g->gl_pathc = 0;
		g->gl_pathv = 0;
		if (!(flags & GLOB_DOOFFS))
			g->gl_offs = 0;
	}
	int budget = 10000;
	int r = (flags & GLOB_BRACE) ? braces(c, pat, &budget) : glob_one(c, pat);
	int magic = has_magic(pat, strlen(pat), flags & GLOB_NOESCAPE);
	if (!r && !l.n) {
		if ((flags & GLOB_NOCHECK) || ((flags & GLOB_NOMAGIC) && !magic)) {
			if (add(c, pat, 0))
				r = GLOB_NOSPACE;
		} else {
			r = GLOB_NOMATCH;
		}
	}
	free(c);
	if (r) {
		for (size_t i = 0; i < l.n; i++)
			free(l.v[i]);
		free(l.v);
		return r;
	}
	size_t offs = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;
	size_t old = (flags & GLOB_APPEND) ? g->gl_pathc : 0;
	char **v = realloc((flags & GLOB_APPEND) ? g->gl_pathv : 0, (offs + old + l.n + 1) * sizeof *v);
	if (!v) {
		for (size_t i = 0; i < l.n; i++)
			free(l.v[i]);
		free(l.v);
		return GLOB_NOSPACE;
	}
	if (!(flags & GLOB_APPEND))
		for (size_t i = 0; i < offs; i++)
			v[i] = 0;
	memcpy(v + offs + old, l.v, l.n * sizeof *v);
	v[offs + old + l.n] = 0;
	free(l.v);
	g->gl_pathv = v;
	g->gl_pathc = old + l.n;
	g->gl_flags = flags | (magic ? GLOB_MAGCHAR : 0);
	return 0;
}

void globfree(glob_t *g)
{
	if (!g->gl_pathv)
		return;
	for (size_t i = 0; i < g->gl_pathc; i++)
		free(g->gl_pathv[g->gl_offs + i]);
	free(g->gl_pathv);
	g->gl_pathv = 0;
	g->gl_pathc = 0;
}
