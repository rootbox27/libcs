/* wordexp: shell word expansion done in-process. Quoting, parameter
 * expansion (all the ${} forms), arithmetic, tilde, field splitting and
 * pathname expansion never involve a shell; only command substitution
 * runs /bin/sh. With WRDE_NOCMD a pre-scan refuses any $(...) or `...`
 * outside single quotes before anything is expanded, so a substitution
 * hidden in a default value like ${x:-`cmd`} cannot slip through.
 *
 * There are no positional parameters: $1, $@ and $* are empty, $# is 0. */
#include <wordexp.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MAX_DEPTH 64
#define MAX_NEST 256 /* brackets and quotes inside one construct */
#define MAX_CMD_OUTPUT (64 << 20)

struct buf {
	char *s;
	size_t n, cap;
};

struct words {
	char **v;
	size_t n, cap;
};

struct st {
	int flags, err, depth;
	struct words *out;
	/* the field being built: raw text and the same text as a glob pattern */
	struct buf raw, pat;
	int in_field, magic, after_ws;
	int split; /* 0 while expanding a ${} word or arithmetic: no fields */
};

static int put(struct st *st, struct buf *b, char c)
{
	if (b->n + 1 >= b->cap) {
		size_t cap = b->cap ? 2 * b->cap : 32;
		char *s = realloc(b->s, cap);
		if (!s) {
			st->err = WRDE_NOSPACE;
			return -1;
		}
		b->s = s;
		b->cap = cap;
	}
	b->s[b->n++] = c;
	b->s[b->n] = 0;
	return 0;
}

static char *take(struct st *st, struct buf *b)
{
	if (!b->s && put(st, b, 0) == 0)
		b->n = 0;
	char *s = b->s;
	b->s = 0;
	b->n = b->cap = 0;
	return s;
}

static int add_word(struct st *st, char *s)
{
	struct words *w = st->out;
	if (!s) {
		st->err = WRDE_NOSPACE;
		return -1;
	}
	if (w->n == w->cap) {
		size_t cap = w->cap ? 2 * w->cap : 8;
		char **v = realloc(w->v, cap * sizeof *v);
		if (!v) {
			free(s);
			st->err = WRDE_NOSPACE;
			return -1;
		}
		w->v = v;
		w->cap = cap;
	}
	w->v[w->n++] = s;
	return 0;
}

/* a literal character of the current field; q = it was quoted */
static int emit(struct st *st, char c, int q)
{
	st->in_field = 1;
	st->after_ws = 0;
	if (put(st, &st->raw, c))
		return -1;
	if (q ? strchr("*?[\\", c) != 0 : c == '\\') {
		if (put(st, &st->pat, '\\'))
			return -1;
	} else if (strchr("*?[", c)) {
		st->magic = 1;
	}
	return put(st, &st->pat, c);
}

static int end_field(struct st *st)
{
	if (!st->in_field)
		return 0;
	st->in_field = 0;
	int magic = st->magic;
	st->magic = 0;
	char *raw = take(st, &st->raw), *pat = take(st, &st->pat);
	if (st->err) {
		free(raw);
		free(pat);
		return -1;
	}
	if (magic) {
		glob_t g;
		int r = glob(pat, 0, 0, &g);
		if (r == 0) {
			free(raw);
			free(pat);
			for (size_t i = 0; i < g.gl_pathc; i++)
				if (add_word(st, strdup(g.gl_pathv[i])))
					break;
			globfree(&g);
			return st->err ? -1 : 0;
		}
		if (r == GLOB_NOSPACE) {
			free(raw);
			free(pat);
			st->err = WRDE_NOSPACE;
			return -1;
		}
	}
	free(pat);
	return add_word(st, raw);
}

static const char *ifs(void)
{
	const char *s = getenv("IFS");
	return s ? s : " \t\n";
}

/* the result of an expansion; unquoted results are split and globbed */
static int emit_value(struct st *st, const char *v, int quoted)
{
	if (quoted || !st->split) {
		if (quoted)
			st->in_field = 1;
		for (; *v; v++)
			if (emit(st, *v, quoted))
				return -1;
		return 0;
	}
	const char *sep = ifs();
	for (; *v; v++) {
		if (!strchr(sep, *v)) {
			if (emit(st, *v, 0))
				return -1;
		} else if (*v == ' ' || *v == '\t' || *v == '\n') {
			if (st->in_field) {
				if (end_field(st))
					return -1;
				st->after_ws = 1;
			}
		} else {
			/* a non-blank separator ends a field even if it is empty,
			 * unless blanks already ended it */
			if (!st->in_field && !st->after_ws)
				st->in_field = 1;
			st->after_ws = 0;
			if (st->in_field && end_field(st))
				return -1;
		}
	}
	return 0;
}

/* ---- scanning helpers ---- */

/* find the end of a quoted or nested construct starting at s[i]; returns
 * the index just past it, or 0 on a syntax error */
static size_t skip_construct(const char *s, size_t i, int d);

static size_t skip_squote(const char *s, size_t i)
{
	for (i++; s[i]; i++)
		if (s[i] == '\'')
			return i + 1;
	return 0;
}

static size_t skip_dquote(const char *s, size_t i, int d)
{
	for (i++; s[i];) {
		if (s[i] == '"')
			return i + 1;
		if (s[i] == '\\' && s[i + 1]) {
			i += 2;
		} else if (s[i] == '$' || s[i] == '`') {
			size_t j = skip_construct(s, i, d + 1);
			if (!j)
				return 0;
			i = j;
		} else {
			i++;
		}
	}
	return 0;
}

static size_t skip_backquote(const char *s, size_t i)
{
	for (i++; s[i]; i++) {
		if (s[i] == '\\' && s[i + 1])
			i++;
		else if (s[i] == '`')
			return i + 1;
	}
	return 0;
}

/* s[i] is '(' or '{'; skip to its matching close, honoring quotes */
static size_t skip_group(const char *s, size_t i, char open, char close, int d)
{
	int depth = 0;
	if (d > MAX_NEST)
		return 0;
	while (s[i]) {
		char c = s[i];
		if (c == '\\' && s[i + 1]) {
			i += 2;
			continue;
		}
		if (c == '\'' || c == '"' || c == '`' || (c == '$' && (s[i + 1] == '(' || s[i + 1] == '{'))) {
			size_t j = c == '\'' ? skip_squote(s, i) : c == '"' ? skip_dquote(s, i, d + 1)
				: c == '`' ? skip_backquote(s, i) : skip_construct(s, i, d + 1);
			if (!j)
				return 0;
			i = j;
			continue;
		}
		if (c == open)
			depth++;
		else if (c == close && --depth == 0)
			return i + 1;
		i++;
	}
	return 0;
}

static size_t skip_construct(const char *s, size_t i, int d)
{
	if (s[i] == '`')
		return skip_backquote(s, i);
	/* s[i] == '$' */
	if (s[i + 1] == '(')
		return skip_group(s, i + 1, '(', ')', d);
	if (s[i + 1] == '{')
		return skip_group(s, i + 1, '{', '}', d);
	return i + 1;
}

/* WRDE_NOCMD: is there a command substitution anywhere outside single
 * quotes? Arithmetic $(( is not one. */
static int has_cmdsub(const char *s)
{
	int dq = 0;
	for (size_t i = 0; s[i]; i++) {
		if (s[i] == '\\' && s[i + 1]) {
			i++;
		} else if (s[i] == '\'' && !dq) {
			size_t j = skip_squote(s, i);
			if (!j)
				return 0; /* the parser reports the syntax error */
			i = j - 1;
		} else if (s[i] == '"') {
			dq = !dq;
		} else if (s[i] == '`') {
			return 1;
		} else if (s[i] == '$' && s[i + 1] == '(') {
			if (s[i + 2] != '(')
				return 1;
			i += 2;
		}
	}
	return 0;
}

/* ---- sub-expansion: expand a word to a single string ---- */

static int parse(struct st *st, const char *s, size_t len);

/* expand s[0..len) without splitting; returns the raw text, and if pat is
 * given, the same text as a glob pattern (quoted parts escaped) */
static char *expand_word(struct st *st, const char *s, size_t len, char **pat)
{
	struct st sub = { .flags = st->flags, .depth = st->depth + 1, .out = st->out, .split = 0 };
	if (sub.depth > MAX_DEPTH) {
		st->err = WRDE_NOSPACE;
		return 0;
	}
	if (parse(&sub, s, len)) {
		st->err = sub.err;
		free(sub.raw.s);
		free(sub.pat.s);
		return 0;
	}
	char *raw = take(&sub, &sub.raw), *p = take(&sub, &sub.pat);
	if (sub.err || !raw || !p) {
		free(raw);
		free(p);
		st->err = WRDE_NOSPACE;
		return 0;
	}
	if (pat)
		*pat = p;
	else
		free(p);
	return raw;
}

/* ---- arithmetic ---- */

struct arith {
	const char *s;
	int err, depth;
};

static long ar_cond(struct arith *a);

static void ar_ws(struct arith *a)
{
	while (*a->s == ' ' || *a->s == '\t' || *a->s == '\n')
		a->s++;
}

static long ar_primary1(struct arith *a);

static long ar_primary(struct arith *a)
{
	if (++a->depth > MAX_NEST) {
		a->err = 1;
		return 0;
	}
	long v = ar_primary1(a);
	a->depth--;
	return v;
}

static long ar_primary1(struct arith *a)
{
	ar_ws(a);
	char c = *a->s;
	if (c == '(') {
		a->s++;
		long v = ar_cond(a);
		ar_ws(a);
		if (*a->s != ')')
			a->err = 1;
		else
			a->s++;
		return v;
	}
	if (c == '-' || c == '+' || c == '~' || c == '!') {
		a->s++;
		long v = ar_primary(a);
		return c == '-' ? (long)(0UL - (unsigned long)v) : c == '+' ? v : c == '~' ? ~v : !v;
	}
	if (c >= '0' && c <= '9') {
		char *e;
		errno = 0;
		unsigned long v = strtoul(a->s, &e, 0);
		if (errno || (*e >= '0' && *e <= '9') || (*e >= 'A' && *e <= 'Z') || (*e >= 'a' && *e <= 'z') || *e == '_')
			a->err = 1;
		a->s = e;
		return (long)v;
	}
	if (c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
		/* a bare variable name holds a number (or nothing, which is 0) */
		char name[256];
		size_t n = 0;
		while (*a->s == '_' || (*a->s >= 'A' && *a->s <= 'Z') || (*a->s >= 'a' && *a->s <= 'z') ||
		       (*a->s >= '0' && *a->s <= '9')) {
			if (n + 1 >= sizeof name) {
				a->err = 1;
				return 0;
			}
			name[n++] = *a->s++;
		}
		name[n] = 0;
		const char *v = getenv(name);
		if (!v || !*v)
			return 0;
		char *e;
		errno = 0;
		long r = strtol(v, &e, 0);
		if (errno || *e)
			a->err = 1;
		return r;
	}
	a->err = 1;
	return 0;
}

enum { P_OROR = 1, P_ANDAND, P_OR, P_XOR, P_AND, P_EQ, P_REL, P_SHIFT, P_ADD, P_MUL };

/* read a binary operator at a->s; returns its precedence (0 = none) */
static int ar_op(struct arith *a, char op[3])
{
	ar_ws(a);
	const char *s = a->s;
	static const struct { const char *t; int p; } ops[] = {
		{ "||", P_OROR }, { "&&", P_ANDAND }, { "==", P_EQ }, { "!=", P_EQ }, { "<=", P_REL }, { ">=", P_REL },
		{ "<<", P_SHIFT }, { ">>", P_SHIFT }, { "|", P_OR }, { "^", P_XOR }, { "&", P_AND }, { "<", P_REL },
		{ ">", P_REL }, { "+", P_ADD }, { "-", P_ADD }, { "*", P_MUL }, { "/", P_MUL }, { "%", P_MUL },
	};
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++) {
		size_t n = strlen(ops[i].t);
		if (!strncmp(s, ops[i].t, n)) {
			memcpy(op, ops[i].t, n + 1);
			return ops[i].p;
		}
	}
	return 0;
}

static long ar_apply(struct arith *a, const char *op, long x, long y)
{
	unsigned long ux = (unsigned long)x, uy = (unsigned long)y;
	switch (op[0]) {
	case '|': return op[1] ? (x || y) : x | y;
	case '&': return op[1] ? (x && y) : x & y;
	case '^': return x ^ y;
	case '=': return x == y;
	case '!': return x != y;
	case '<':
		if (op[1] == '<')
			return y < 0 || y > 63 ? (a->err = 1, 0) : (long)(ux << y);
		return op[1] ? x <= y : x < y;
	case '>':
		if (op[1] == '>')
			return y < 0 || y > 63 ? (a->err = 1, 0) : x >> y;
		return op[1] ? x >= y : x > y;
	case '+': return (long)(ux + uy);
	case '-': return (long)(ux - uy);
	case '*': return (long)(ux * uy);
	default: /* / and % */
		if (!y) {
			a->err = 1;
			return 0;
		}
		if (x == LONG_MIN && y == -1)
			return op[0] == '/' ? LONG_MIN : 0;
		return op[0] == '/' ? x / y : x % y;
	}
}

/* precedence climbing */
static long ar_binary(struct arith *a, int minp)
{
	long x = ar_primary(a);
	for (;;) {
		char op[3];
		const char *save = a->s;
		int p = ar_op(a, op);
		if (!p || p < minp) {
			a->s = save;
			return x;
		}
		a->s += strlen(op);
		long y = ar_binary(a, p + 1);
		x = ar_apply(a, op, x, y);
		if (a->err)
			return 0;
	}
}

static long ar_cond(struct arith *a)
{
	long c = ar_binary(a, P_OROR);
	ar_ws(a);
	if (*a->s != '?')
		return c;
	a->s++;
	if (++a->depth > MAX_NEST) {
		a->err = 1;
		return 0;
	}
	long x = ar_cond(a);
	ar_ws(a);
	if (*a->s != ':') {
		a->err = 1;
		return 0;
	}
	a->s++;
	long y = ar_cond(a);
	a->depth--;
	return c ? x : y;
}

/* ---- command substitution ---- */

static char *run_command(struct st *st, const char *cmd)
{
	if (st->flags & WRDE_NOCMD) {
		st->err = WRDE_CMDSUB;
		return 0;
	}
	int p[2];
	if (pipe2(p, O_CLOEXEC)) {
		st->err = WRDE_NOSPACE;
		return 0;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1);
	if (!(st->flags & WRDE_SHOWERR))
		posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
	char *argv[] = { (char *)"sh", (char *)"-c", (char *)cmd, 0 };
	pid_t pid;
	int r = posix_spawn(&pid, "/bin/sh", &fa, 0, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	close(p[1]);
	if (r) {
		close(p[0]);
		st->err = WRDE_NOSPACE;
		return 0;
	}
	struct buf b = { 0 };
	char chunk[4096];
	ssize_t n;
	while ((n = read(p[0], chunk, sizeof chunk)) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		for (ssize_t i = 0; i < n && !st->err; i++)
			if (chunk[i] && put(st, &b, chunk[i])) /* NULs cannot be represented */
				break;
		if (st->err || b.n > MAX_CMD_OUTPUT) {
			st->err = WRDE_NOSPACE;
			break;
		}
	}
	close(p[0]);
	while (waitpid(pid, 0, 0) < 0 && errno == EINTR)
		;
	if (st->err) {
		free(b.s);
		return 0;
	}
	char *s = take(st, &b);
	if (s) {
		size_t l = strlen(s);
		while (l && s[l - 1] == '\n')
			s[--l] = 0;
	}
	return s;
}

/* ---- parameters ---- */

static int is_name_start(char c)
{
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_name(char c)
{
	return is_name_start(c) || (c >= '0' && c <= '9');
}

/* value of a parameter, or 0 if unset; numbuf holds $$ */
static const char *param(const char *name, size_t n, char *numbuf)
{
	if (n == 1 && name[0] == '$') {
		snprintf(numbuf, 24, "%d", (int)getpid());
		return numbuf;
	}
	if (n == 1 && (name[0] == '#' || name[0] == '?'))
		return "0";
	if (n == 1 && name[0] == '-')
		return "";
	if ((n == 1 && (name[0] == '@' || name[0] == '*' || name[0] == '!')) || (name[0] >= '0' && name[0] <= '9'))
		return 0;
	char key[256];
	if (n >= sizeof key)
		return 0;
	memcpy(key, name, n);
	key[n] = 0;
	return getenv(key);
}

/* remove the shortest/longest prefix/suffix matching pat */
static char *trim(const char *v, const char *pat, int suffix, int longest)
{
	size_t n = strlen(v);
	char *w = strdup(v);
	if (!w)
		return 0;
	for (size_t k = 0; k <= n; k++) {
		size_t len = longest ? n - k : k; /* length of the part removed */
		if (suffix) {
			if (!fnmatch(pat, v + n - len, 0)) {
				w[n - len] = 0;
				return w;
			}
		} else {
			char c = w[len];
			w[len] = 0;
			int m = !fnmatch(pat, w, 0);
			w[len] = c;
			if (m) {
				memmove(w, w + len, n - len + 1);
				return w;
			}
		}
	}
	return w;
}

/* ${...}: body is the text between the braces */
static int brace_param(struct st *st, const char *body, size_t len, int quoted)
{
	char numbuf[24];
	size_t i = 0;
	int length = 0;
	if (len > 1 && body[0] == '#') {
		length = 1;
		i = 1;
	}
	size_t ns = i;
	if (i < len && is_name_start(body[i])) {
		while (i < len && is_name(body[i]))
			i++;
	} else if (i < len && body[i] >= '0' && body[i] <= '9') {
		while (i < len && body[i] >= '0' && body[i] <= '9')
			i++;
	} else if (i < len && strchr("$#?-@*!", body[i])) {
		i++;
	}
	size_t nl = i - ns;
	if (!nl) {
		st->err = WRDE_SYNTAX;
		return -1;
	}
	const char *v = param(body + ns, nl, numbuf);
	if (length) {
		if (i != len) {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		if (!v && (st->flags & WRDE_UNDEF)) {
			st->err = WRDE_BADVAL;
			return -1;
		}
		snprintf(numbuf, sizeof numbuf, "%zu", v ? strlen(v) : 0);
		return emit_value(st, numbuf, quoted);
	}
	if (i == len) {
		if (!v && (st->flags & WRDE_UNDEF)) {
			st->err = WRDE_BADVAL;
			return -1;
		}
		return emit_value(st, v ? v : "", quoted);
	}
	int colon = body[i] == ':';
	if (colon)
		i++;
	if (i >= len) {
		st->err = WRDE_SYNTAX;
		return -1;
	}
	char op = body[i++];
	int twice = 0;
	if ((op == '%' || op == '#') && !colon && i < len && body[i] == op) {
		twice = 1;
		i++;
	}
	if (!strchr(colon ? "-=?+" : "-=?+%#", op)) {
		st->err = WRDE_SYNTAX;
		return -1;
	}
	const char *word = body + i;
	size_t wl = len - i;
	int use = colon ? v && *v : v != 0; /* is the parameter "set" for this op */

	if (op == '%' || op == '#') {
		if (!v && (st->flags & WRDE_UNDEF)) {
			st->err = WRDE_BADVAL;
			return -1;
		}
		char *pat;
		char *raw = expand_word(st, word, wl, &pat);
		if (!raw)
			return -1;
		free(raw);
		char *r = trim(v ? v : "", pat, op == '%', twice);
		free(pat);
		if (!r) {
			st->err = WRDE_NOSPACE;
			return -1;
		}
		int e = emit_value(st, r, quoted);
		free(r);
		return e;
	}
	if (op == '+') {
		if (!use)
			return quoted ? (st->in_field = 1, 0) : 0;
	} else if (use) {
		return emit_value(st, v, quoted);
	}
	/* the word is needed: -, =, ? on an unset/empty parameter, or + on a set one */
	char *w = expand_word(st, word, wl, 0);
	if (!w)
		return -1;
	if (op == '?') {
		if (st->flags & WRDE_SHOWERR) {
			char msg[512];
			int m = snprintf(msg, sizeof msg, "%.*s: %s\n", (int)nl, body + ns,
			                 *w ? w : "parameter null or not set");
			if (m > 0)
				write(2, msg, (size_t)m < sizeof msg ? (size_t)m : sizeof msg - 1);
		}
		free(w);
		st->err = WRDE_BADVAL;
		return -1;
	}
	if (op == '=') {
		char key[256];
		if (!is_name_start(body[ns]) || nl >= sizeof key) {
			free(w);
			st->err = WRDE_SYNTAX;
			return -1;
		}
		memcpy(key, body + ns, nl);
		key[nl] = 0;
		if (setenv(key, w, 1)) {
			free(w);
			st->err = WRDE_NOSPACE;
			return -1;
		}
	}
	int e = emit_value(st, w, quoted);
	free(w);
	return e;
}

/* s[*ip] == '$' or '`': expand it; quoted = inside double quotes */
static int dollar(struct st *st, const char *s, size_t len, size_t *ip, int quoted)
{
	size_t i = *ip;
	if (s[i] == '`') {
		size_t j = skip_backquote(s, i);
		if (!j || j > len) {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		/* inside backquotes, \ quotes only $ ` and \ */
		struct buf cmd = { 0 };
		for (size_t k = i + 1; k < j - 1; k++) {
			if (s[k] == '\\' && k + 1 < j - 1 && strchr("$`\\", s[k + 1]))
				k++;
			if (put(st, &cmd, s[k]))
				break;
		}
		char *c = st->err ? 0 : take(st, &cmd);
		free(cmd.s);
		if (!c) {
			st->err = st->err ? st->err : WRDE_NOSPACE;
			return -1;
		}
		char *out = run_command(st, c);
		free(c);
		if (!out)
			return -1;
		int e = emit_value(st, out, quoted);
		free(out);
		*ip = j;
		return e;
	}
	char c = i + 1 < len ? s[i + 1] : 0;
	if (c == '(' && i + 2 < len && s[i + 2] == '(') {
		/* $(( arithmetic )) */
		size_t j = skip_group(s, i + 1, '(', ')', 0);
		if (!j || j > len || s[j - 2] != ')') {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		char *expr = expand_word(st, s + i + 3, j - i - 5, 0);
		if (!expr)
			return -1;
		struct arith a = { expr, 0, 0 };
		long v = ar_cond(&a);
		ar_ws(&a);
		int bad = a.err || *a.s;
		free(expr);
		if (bad) {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		char num[24];
		snprintf(num, sizeof num, "%ld", v);
		*ip = j;
		return emit_value(st, num, quoted);
	}
	if (c == '(') {
		size_t j = skip_group(s, i + 1, '(', ')', 0);
		if (!j || j > len) {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		char *cmd = strndup(s + i + 2, j - i - 3);
		if (!cmd) {
			st->err = WRDE_NOSPACE;
			return -1;
		}
		char *out = run_command(st, cmd);
		free(cmd);
		if (!out)
			return -1;
		int e = emit_value(st, out, quoted);
		free(out);
		*ip = j;
		return e;
	}
	if (c == '{') {
		size_t j = skip_group(s, i + 1, '{', '}', 0);
		if (!j || j > len) {
			st->err = WRDE_SYNTAX;
			return -1;
		}
		*ip = j;
		return brace_param(st, s + i + 2, j - i - 3, quoted);
	}
	size_t n = 0;
	if (is_name_start(c)) {
		while (i + 1 + n < len && is_name(s[i + 1 + n]))
			n++;
	} else if (c && strchr("$#?-@*!0123456789", c)) {
		n = 1;
	} else {
		/* a lone $ is literal */
		*ip = i + 1;
		return emit(st, '$', quoted);
	}
	char numbuf[24];
	const char *v = param(s + i + 1, n, numbuf);
	*ip = i + 1 + n;
	if (!v && (st->flags & WRDE_UNDEF) && !(n == 1 && strchr("@*", c))) {
		st->err = WRDE_BADVAL;
		return -1;
	}
	if (!v && quoted && n == 1 && c == '@')
		return 0; /* "$@" with no parameters is no field at all */
	return emit_value(st, v ? v : "", quoted);
}

/* ~ or ~user at the start of a word; returns 1 if handled */
static int tilde(struct st *st, const char *s, size_t len, size_t *ip)
{
	size_t i = *ip + 1, e = i;
	while (e < len && s[e] != '/' && s[e] != ' ' && s[e] != '\t')
		e++;
	const char *home = 0;
	if (e == i) {
		home = getenv("HOME");
		if (!home) {
			struct passwd *pw = getpwuid(getuid());
			home = pw ? pw->pw_dir : 0;
		}
	} else {
		char user[LOGIN_NAME_MAX + 1];
		for (size_t k = i; k < e; k++)
			if (!is_name(s[k]) && s[k] != '-' && s[k] != '.')
				return 0; /* quoting or expansions: leave it literal */
		if (e - i >= sizeof user)
			return 0;
		memcpy(user, s + i, e - i);
		user[e - i] = 0;
		struct passwd *pw = getpwnam(user);
		home = pw ? pw->pw_dir : 0;
	}
	if (!home)
		return 0;
	for (; *home; home++)
		if (emit(st, *home, 1))
			return -1;
	st->in_field = 1;
	*ip = e;
	return 1;
}

static int parse(struct st *st, const char *s, size_t len)
{
	for (size_t i = 0; i < len;) {
		char c = s[i];
		if (st->split && (c == ' ' || c == '\t')) {
			if (end_field(st))
				return -1;
			st->after_ws = 0;
			i++;
			continue;
		}
		if (st->split && strchr("\n|&;<>(){}", c)) {
			st->err = WRDE_BADCHAR;
			return -1;
		}
		if (c == '\\') {
			if (i + 1 >= len) {
				st->err = WRDE_SYNTAX;
				return -1;
			}
			if (emit(st, s[i + 1], 1))
				return -1;
			i += 2;
		} else if (c == '\'') {
			size_t j = skip_squote(s, i);
			if (!j || j > len) {
				st->err = WRDE_SYNTAX;
				return -1;
			}
			st->in_field = 1;
			for (size_t k = i + 1; k < j - 1; k++)
				if (emit(st, s[k], 1))
					return -1;
			i = j;
		} else if (c == '"') {
			st->in_field = 1;
			for (i++;;) {
				if (i >= len) {
					st->err = WRDE_SYNTAX;
					return -1;
				}
				char d = s[i];
				if (d == '"') {
					i++;
					break;
				}
				if (d == '\\' && i + 1 < len && strchr("$`\"\\\n", s[i + 1])) {
					if (s[i + 1] != '\n' && emit(st, s[i + 1], 1))
						return -1;
					i += 2;
				} else if (d == '$' || d == '`') {
					if (dollar(st, s, len, &i, 1))
						return -1;
				} else {
					if (emit(st, d, 1))
						return -1;
					i++;
				}
			}
		} else if (c == '$' || c == '`') {
			if (dollar(st, s, len, &i, 0))
				return -1;
		} else if (c == '~' && !st->in_field && st->split) {
			int r = tilde(st, s, len, &i);
			if (r < 0)
				return -1;
			if (!r) {
				if (emit(st, c, 0))
					return -1;
				i++;
			}
		} else {
			if (emit(st, c, 0))
				return -1;
			i++;
		}
	}
	return 0;
}

static void free_words(struct words *w)
{
	for (size_t i = 0; i < w->n; i++)
		free(w->v[i]);
	free(w->v);
}

int wordexp(const char *restrict s, wordexp_t *restrict we, int flags)
{
	if (flags & WRDE_REUSE) {
		wordfree(we);
		flags &= ~WRDE_APPEND;
	}
	if ((flags & WRDE_NOCMD) && has_cmdsub(s))
		return WRDE_CMDSUB;
	struct words out = { 0 };
	struct st st = { .flags = flags, .out = &out, .split = 1 };
	if (parse(&st, s, strlen(s)) == 0)
		end_field(&st);
	free(st.raw.s);
	free(st.pat.s);
	if (st.err) {
		free_words(&out);
		return st.err;
	}
	size_t offs = (flags & WRDE_DOOFFS) ? we->we_offs : 0;
	size_t old = (flags & WRDE_APPEND) ? we->we_wordc : 0;
	if (out.n > (SIZE_MAX / sizeof(char *)) - offs - old - 1) {
		free_words(&out);
		return WRDE_NOSPACE;
	}
	char **v = realloc((flags & WRDE_APPEND) ? we->we_wordv : 0, (offs + old + out.n + 1) * sizeof *v);
	if (!v) {
		free_words(&out);
		return WRDE_NOSPACE;
	}
	if (!(flags & WRDE_APPEND)) {
		for (size_t i = 0; i < offs; i++)
			v[i] = 0;
		if (!(flags & WRDE_DOOFFS))
			we->we_offs = 0;
	}
	if (out.n)
		memcpy(v + offs + old, out.v, out.n * sizeof *v);
	v[offs + old + out.n] = 0;
	free(out.v);
	we->we_wordv = v;
	we->we_wordc = old + out.n;
	return 0;
}

void wordfree(wordexp_t *we)
{
	if (!we->we_wordv)
		return;
	for (size_t i = 0; i < we->we_wordc; i++)
		free(we->we_wordv[we->we_offs + i]);
	free(we->we_wordv);
	we->we_wordv = 0;
	we->we_wordc = 0;
}
