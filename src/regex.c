/* POSIX regular expressions (BRE and ERE, with the usual GNU extensions
 * \| \+ \? in BREs, \< \> \b \B \w \W \s \S \` \' and back-references in
 * EREs).
 *
 * Patterns are parsed into a tree and compiled to a small instruction
 * set. Without back-references they run on a Pike VM: all threads advance
 * in lock step, so matching is linear in the text length and there is no
 * catastrophic backtracking. The match is leftmost-longest overall;
 * subexpressions follow priority order (greedy repetition, alternatives
 * left first), as in other linear-time POSIX engines. Patterns with
 * back-references need a backtracking search, which is bounded by a step
 * budget and fails with REG_ESPACE rather than running unboundedly.
 * Text and patterns are UTF-8; invalid bytes match only themselves. */
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define BAD 0x200000        /* BAD + byte: an undecodable byte */
#define MAX_INST 50000
#define BT_BUDGET 20000000L /* backtracking steps per regexec */

/* ---- UTF-8 ---- */

static int dec(const unsigned char *s, size_t len, int *c)
{
	unsigned b = s[0];
	if (b < 0x80) {
		*c = (int)b;
		return 1;
	}
	int n = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc2 ? 2 : 0;
	if (!n || b > 0xf4 || (size_t)n > len)
		goto bad;
	unsigned v = b & (0x7f >> n);
	for (int i = 1; i < n; i++) {
		if ((s[i] & 0xc0) != 0x80)
			goto bad;
		v = v << 6 | (s[i] & 0x3f);
	}
	if ((n == 3 && v < 0x800) || (n == 4 && (v < 0x10000 || v > 0x10ffff)) || (v >= 0xd800 && v <= 0xdfff))
		goto bad;
	*c = (int)v;
	return n;
bad:
	*c = BAD + (int)b;
	return 1;
}

/* ---- program ---- */

enum { I_CHAR, I_ANY, I_SET, I_ASSERT, I_SPLIT, I_JMP, I_SAVE, I_MATCH, I_BREF, I_ENTER, I_CHECK };
enum { A_BOL, A_EOL, A_WORDB, A_NWORDB, A_WBEG, A_WEND, A_BUFBEG, A_BUFEND };

struct inst {
	unsigned char op;
	int x, y, z;
};

struct range { int lo, hi; };

struct set {
	int neg;
	uint32_t bm[8];
	struct range *r;
	int nr, rcap;
	wctype_t cls[16];
	int ncls;
};

struct prog {
	struct inst *code;
	int n;
	struct set *sets;
	int nsets, setcap;
	int nsub, nloops;
	int flags;
	int backrefs;
};

/* ---- parse tree ---- */

enum { N_EMPTY, N_CHAR, N_ANY, N_SET, N_ASSERT, N_CAT, N_ALT, N_REP, N_GROUP, N_BREF };

struct node {
	int type, a, b, c;
};

struct parser {
	const unsigned char *s;
	size_t len, pos;
	int ere, icase, newline;
	struct node *nodes;
	int nn, ncap;
	struct prog *p;
	int nsub, err;
	unsigned long long closed; /* groups 1..63 that are complete */
	int depth;
};

static int mknode(struct parser *ps, int type, int a, int b, int c)
{
	if (ps->err)
		return -1;
	if (ps->nn == ps->ncap) {
		int cap = ps->ncap ? 2 * ps->ncap : 64;
		if (cap > 1 << 20) {
			ps->err = REG_ESPACE;
			return -1;
		}
		struct node *n = realloc(ps->nodes, (size_t)cap * sizeof *n);
		if (!n) {
			ps->err = REG_ESPACE;
			return -1;
		}
		ps->nodes = n;
		ps->ncap = cap;
	}
	ps->nodes[ps->nn] = (struct node){ type, a, b, c };
	return ps->nn++;
}

static int peek(struct parser *ps, size_t off)
{
	return ps->pos + off < ps->len ? ps->s[ps->pos + off] : -1;
}

static int getc_pat(struct parser *ps)
{
	int c;
	ps->pos += (size_t)dec(ps->s + ps->pos, ps->len - ps->pos, &c);
	return c;
}

static struct set *newset(struct parser *ps, int *idx)
{
	struct prog *p = ps->p;
	if (p->nsets == p->setcap) {
		int cap = p->setcap ? 2 * p->setcap : 8;
		struct set *s = realloc(p->sets, (size_t)cap * sizeof *s);
		if (!s) {
			ps->err = REG_ESPACE;
			return 0;
		}
		p->sets = s;
		p->setcap = cap;
	}
	*idx = p->nsets;
	struct set *s = &p->sets[p->nsets++];
	memset(s, 0, sizeof *s);
	return s;
}

static int set_add(struct parser *ps, struct set *s, int lo, int hi)
{
	for (; lo <= hi && lo < 256; lo++)
		s->bm[lo >> 5] |= 1u << (lo & 31);
	if (lo > hi)
		return 0;
	if (s->nr == s->rcap) {
		int cap = s->rcap ? 2 * s->rcap : 4;
		struct range *r = realloc(s->r, (size_t)cap * sizeof *r);
		if (!r) {
			ps->err = REG_ESPACE;
			return -1;
		}
		s->r = r;
		s->rcap = cap;
	}
	s->r[s->nr++] = (struct range){ lo, hi };
	return 0;
}

static int in_set_raw(const struct set *s, int c)
{
	if (c < 256 && (s->bm[c >> 5] >> (c & 31) & 1))
		return 1;
	for (int i = 0; i < s->nr; i++)
		if (c >= s->r[i].lo && c <= s->r[i].hi)
			return 1;
	for (int i = 0; i < s->ncls; i++)
		if (iswctype((wint_t)c, s->cls[i]))
			return 1;
	return 0;
}

/* one element of a bracket expression: a character, [.c.] or [=c=];
 * returns the character, or -1 with ps->err set */
static int bracket_char(struct parser *ps)
{
	if (peek(ps, 0) == '[' && (peek(ps, 1) == '.' || peek(ps, 1) == '=')) {
		int kind = peek(ps, 1);
		ps->pos += 2;
		if (ps->pos >= ps->len) {
			ps->err = REG_EBRACK;
			return -1;
		}
		int c = getc_pat(ps);
		if (peek(ps, 0) != kind || peek(ps, 1) != ']') {
			ps->err = ps->pos >= ps->len ? REG_EBRACK : REG_ECOLLATE;
			return -1;
		}
		ps->pos += 2;
		return c;
	}
	return getc_pat(ps);
}

static int parse_bracket(struct parser *ps)
{
	int idx;
	struct set *s = newset(ps, &idx);
	if (!s)
		return -1;
	if (peek(ps, 0) == '^') {
		s->neg = 1;
		ps->pos++;
	}
	int first = 1;
	for (;;) {
		if (ps->pos >= ps->len) {
			ps->err = REG_EBRACK;
			return -1;
		}
		s = &ps->p->sets[idx]; /* sets may move while parsing */
		if (peek(ps, 0) == ']' && !first) {
			ps->pos++;
			break;
		}
		first = 0;
		if (peek(ps, 0) == '[' && peek(ps, 1) == ':') {
			size_t start = ps->pos + 2, e = start;
			while (e + 1 < ps->len && !(ps->s[e] == ':' && ps->s[e + 1] == ']'))
				e++;
			if (e + 1 >= ps->len) {
				ps->err = REG_EBRACK;
				return -1;
			}
			char name[16];
			if (e - start >= sizeof name) {
				ps->err = REG_ECTYPE;
				return -1;
			}
			memcpy(name, ps->s + start, e - start);
			name[e - start] = 0;
			wctype_t t = wctype(name);
			if (!t || s->ncls == 16) {
				ps->err = REG_ECTYPE;
				return -1;
			}
			if (ps->icase && (!strcmp(name, "upper") || !strcmp(name, "lower")))
				t = wctype("alpha");
			s->cls[s->ncls++] = t;
			ps->pos = e + 2;
			continue;
		}
		int lo = bracket_char(ps);
		if (lo < 0)
			return -1;
		int hi = lo;
		if (peek(ps, 0) == '-' && peek(ps, 1) != ']' && peek(ps, 1) != -1) {
			ps->pos++;
			if (peek(ps, 0) == '[' && peek(ps, 1) == ':') {
				ps->err = REG_ERANGE;
				return -1;
			}
			hi = bracket_char(ps);
			if (hi < 0)
				return -1;
			if (hi < lo || lo >= BAD || hi >= BAD) {
				ps->err = REG_ERANGE;
				return -1;
			}
		}
		if (set_add(ps, s, lo, hi) < 0)
			return -1;
		if (ps->icase) {
			/* add the other cases of the range's letters */
			for (int c = lo; c <= hi && c - lo < 0x3000; c++) {
				int l = (int)towlower((wint_t)c), u = (int)towupper((wint_t)c);
				if (l != c && set_add(ps, &ps->p->sets[idx], l, l) < 0)
					return -1;
				if (u != c && set_add(ps, &ps->p->sets[idx], u, u) < 0)
					return -1;
			}
		}
	}
	return mknode(ps, N_SET, idx, 0, 0);
}

/* a single-class set for \w \s and their negations */
static int class_set(struct parser *ps, const char *cls, int neg, int underscore)
{
	int idx;
	struct set *s = newset(ps, &idx);
	if (!s)
		return -1;
	s->neg = neg;
	s->cls[s->ncls++] = wctype(cls);
	if (underscore && set_add(ps, s, '_', '_') < 0)
		return -1;
	return mknode(ps, N_SET, idx, 0, 0);
}

static int parse_alt(struct parser *ps);

static int at_alt(struct parser *ps)
{
	return ps->ere ? peek(ps, 0) == '|' : peek(ps, 0) == '\\' && peek(ps, 1) == '|';
}

static int at_close(struct parser *ps)
{
	if (ps->ere)
		return peek(ps, 0) == ')' && ps->depth > 0;
	return peek(ps, 0) == '\\' && peek(ps, 1) == ')';
}

/* are we at the start of an RE (or subexpression, or alternative)? */
static int at_start(struct parser *ps, size_t atom_start)
{
	if (atom_start == 0)
		return 1;
	const unsigned char *s = ps->s;
	if (ps->ere)
		return s[atom_start - 1] == '(' || s[atom_start - 1] == '|';
	if (atom_start >= 2 && s[atom_start - 2] == '\\' && (s[atom_start - 1] == '(' || s[atom_start - 1] == '|'))
		return 1;
	return 0;
}

static int parse_atom(struct parser *ps, size_t seq_start)
{
	int c = peek(ps, 0);
	size_t here = ps->pos;
	if (ps->ere) {
		switch (c) {
		case '(': {
			ps->pos++;
			int n = ++ps->nsub;
			ps->depth++;
			int in = at_close(ps) ? mknode(ps, N_EMPTY, 0, 0, 0) : parse_alt(ps);
			ps->depth--;
			if (in < 0)
				return -1;
			if (peek(ps, 0) != ')') {
				ps->err = REG_EPAREN;
				return -1;
			}
			ps->pos++;
			if (n < 64)
				ps->closed |= 1ull << n;
			return mknode(ps, N_GROUP, in, n, 0);
		}
		case '*': case '+': case '?':
			ps->err = REG_BADRPT;
			return -1;
		case '{':
			if (peek(ps, 1) >= '0' && peek(ps, 1) <= '9') {
				ps->err = REG_BADRPT;
				return -1;
			}
			break;
		case '^':
			ps->pos++;
			return mknode(ps, N_ASSERT, A_BOL, 0, 0);
		case '$':
			ps->pos++;
			return mknode(ps, N_ASSERT, A_EOL, 0, 0);
		}
	} else {
		if (c == '^' && at_start(ps, here)) {
			ps->pos++;
			return mknode(ps, N_ASSERT, A_BOL, 0, 0);
		}
		if (c == '$') {
			/* an anchor only at the end of the RE or a subexpression */
			if (ps->pos + 1 == ps->len || (peek(ps, 1) == '\\' && (peek(ps, 2) == ')' || peek(ps, 2) == '|'))) {
				ps->pos++;
				return mknode(ps, N_ASSERT, A_EOL, 0, 0);
			}
		}
		if (c == '*' && (at_start(ps, here) || (here == seq_start + 1 && ps->s[seq_start] == '^'))) {
			ps->pos++;
			return mknode(ps, N_CHAR, '*', 0, 0);
		}
		if (c == '*') {
			ps->pos++;
			return mknode(ps, N_CHAR, '*', 0, 0);
		}
	}
	if (c == '.') {
		ps->pos++;
		return mknode(ps, N_ANY, 0, 0, 0);
	}
	if (c == '[') {
		ps->pos++;
		return parse_bracket(ps);
	}
	if (c == '\\') {
		if (ps->pos + 1 >= ps->len) {
			ps->err = REG_EESCAPE;
			return -1;
		}
		ps->pos++;
		int e = peek(ps, 0);
		if (!ps->ere && e == '(') {
			ps->pos++;
			int n = ++ps->nsub;
			ps->depth++;
			int in = at_close(ps) ? mknode(ps, N_EMPTY, 0, 0, 0) : parse_alt(ps);
			ps->depth--;
			if (in < 0)
				return -1;
			if (!(peek(ps, 0) == '\\' && peek(ps, 1) == ')')) {
				ps->err = REG_EPAREN;
				return -1;
			}
			ps->pos += 2;
			if (n < 64)
				ps->closed |= 1ull << n;
			return mknode(ps, N_GROUP, in, n, 0);
		}
		if (!ps->ere && e == ')') {
			ps->err = REG_EPAREN;
			return -1;
		}
		if (!ps->ere && e == '{') {
			ps->err = REG_BADRPT;
			return -1;
		}
		if (e >= '1' && e <= '9') {
			int n = e - '0';
			ps->pos++;
			if (n > ps->nsub || !(ps->closed >> n & 1)) {
				ps->err = REG_ESUBREG;
				return -1;
			}
			ps->p->backrefs = 1;
			return mknode(ps, N_BREF, n, 0, 0);
		}
		ps->pos++;
		switch (e) {
		case '<': return mknode(ps, N_ASSERT, A_WBEG, 0, 0);
		case '>': return mknode(ps, N_ASSERT, A_WEND, 0, 0);
		case 'b': return mknode(ps, N_ASSERT, A_WORDB, 0, 0);
		case 'B': return mknode(ps, N_ASSERT, A_NWORDB, 0, 0);
		case '`': return mknode(ps, N_ASSERT, A_BUFBEG, 0, 0);
		case '\'': return mknode(ps, N_ASSERT, A_BUFEND, 0, 0);
		case 'w': return class_set(ps, "alnum", 0, 1);
		case 'W': return class_set(ps, "alnum", 1, 1);
		case 's': return class_set(ps, "space", 0, 0);
		case 'S': return class_set(ps, "space", 1, 0);
		}
		ps->pos--;
		c = getc_pat(ps);
		return mknode(ps, N_CHAR, ps->icase ? (int)towlower((wint_t)c) : c, 0, 0);
	}
	c = getc_pat(ps);
	return mknode(ps, N_CHAR, ps->icase && c < BAD ? (int)towlower((wint_t)c) : c, 0, 0);
}

/* {m}, {m,}, {m,n}; ps->pos is just past the opening brace */
static int parse_interval(struct parser *ps, int *mn, int *mx)
{
	int m = 0, n, digits = 0;
	while (peek(ps, 0) >= '0' && peek(ps, 0) <= '9') {
		m = m * 10 + (peek(ps, 0) - '0');
		if (m > RE_DUP_MAX)
			m = RE_DUP_MAX + 1;
		ps->pos++;
		digits++;
	}
	if (!digits && (!ps->ere || peek(ps, 0) != ',')) {
		ps->err = ps->pos >= ps->len ? REG_EBRACE : REG_BADBR;
		return -1;
	}
	n = m;
	if (peek(ps, 0) == ',') {
		ps->pos++;
		n = -1;
		if (peek(ps, 0) >= '0' && peek(ps, 0) <= '9') {
			n = 0;
			while (peek(ps, 0) >= '0' && peek(ps, 0) <= '9') {
				n = n * 10 + (peek(ps, 0) - '0');
				if (n > RE_DUP_MAX)
					n = RE_DUP_MAX + 1;
				ps->pos++;
			}
		}
	}
	if (ps->ere ? peek(ps, 0) != '}' : !(peek(ps, 0) == '\\' && peek(ps, 1) == '}')) {
		ps->err = ps->pos >= ps->len ? REG_EBRACE : REG_BADBR;
		return -1;
	}
	ps->pos += ps->ere ? 1 : 2;
	if (m > RE_DUP_MAX || n > RE_DUP_MAX || (n >= 0 && n < m)) {
		ps->err = REG_BADBR;
		return -1;
	}
	*mn = m;
	*mx = n;
	return 0;
}

static int parse_repeat(struct parser *ps, size_t seq_start)
{
	int a = parse_atom(ps, seq_start);
	if (a < 0)
		return -1;
	for (;;) {
		int c = peek(ps, 0), mn, mx;
		if (c == '*') {
			ps->pos++;
			mn = 0, mx = -1;
		} else if (ps->ere && (c == '+' || c == '?')) {
			ps->pos++;
			mn = c == '+', mx = c == '+' ? -1 : 1;
		} else if (ps->ere && c == '{' && ((peek(ps, 1) >= '0' && peek(ps, 1) <= '9') || peek(ps, 1) == ',')) {
			ps->pos++;
			if (parse_interval(ps, &mn, &mx) < 0)
				return -1;
		} else if (!ps->ere && c == '\\' && (peek(ps, 1) == '+' || peek(ps, 1) == '?')) {
			mn = peek(ps, 1) == '+', mx = peek(ps, 1) == '+' ? -1 : 1;
			ps->pos += 2;
		} else if (!ps->ere && c == '\\' && peek(ps, 1) == '{') {
			ps->pos += 2;
			if (parse_interval(ps, &mn, &mx) < 0)
				return -1;
		} else {
			return a;
		}
		if (!ps->ere && ps->nodes[a].type == N_ASSERT && ps->nodes[a].a == A_BOL) {
			/* "^*": the star is an ordinary character */
			ps->pos -= c == '*' ? 1 : 2;
			if (c != '*')
				return a;
			ps->pos++;
			int lit = mknode(ps, N_CHAR, '*', 0, 0);
			return lit < 0 ? -1 : mknode(ps, N_CAT, a, lit, 0);
		}
		if (ps->nodes[a].type == N_ASSERT) {
			if (ps->ere) {
				ps->err = REG_BADRPT;
				return -1;
			}
		}
		a = mknode(ps, N_REP, a, mn, mx);
		if (a < 0)
			return -1;
	}
}

static int parse_cat(struct parser *ps)
{
	int seq = mknode(ps, N_EMPTY, 0, 0, 0);
	size_t start = ps->pos;
	while (seq >= 0 && ps->pos < ps->len && !at_alt(ps) && !at_close(ps)) {
		int a = parse_repeat(ps, start);
		if (a < 0)
			return -1;
		seq = ps->nodes[seq].type == N_EMPTY ? a : mknode(ps, N_CAT, seq, a, 0);
	}
	return seq;
}

static int parse_alt(struct parser *ps)
{
	int l = parse_cat(ps);
	while (l >= 0 && at_alt(ps)) {
		ps->pos += ps->ere ? 1 : 2;
		int r = parse_cat(ps);
		if (r < 0)
			return -1;
		l = mknode(ps, N_ALT, l, r, 0);
	}
	return l;
}

/* ---- code generation ---- */

static long size_of(struct parser *ps, int n)
{
	const struct node *x = &ps->nodes[n];
	long s;
	switch (x->type) {
	case N_EMPTY: return 0;
	case N_CAT: s = size_of(ps, x->a) + size_of(ps, x->b); break;
	case N_ALT: s = size_of(ps, x->a) + size_of(ps, x->b) + 2; break;
	case N_GROUP: s = size_of(ps, x->a) + 2; break;
	case N_REP: {
		long c = size_of(ps, x->a);
		s = x->c < 0 ? (x->b ? x->b * c + 2 : c + 3) : x->b * c + (long)(x->c - x->b) * (c + 1);
		break;
	}
	default: return 1;
	}
	return s > MAX_INST ? MAX_INST + 1 : s;
}

static int emit(struct prog *p, int op, int x, int y)
{
	p->code[p->n] = (struct inst){ (unsigned char)op, x, y, 0 };
	return p->n++;
}

static void gen(struct parser *ps, int n)
{
	struct prog *p = ps->p;
	const struct node x = ps->nodes[n];
	switch (x.type) {
	case N_EMPTY:
		break;
	case N_CHAR: emit(p, I_CHAR, x.a, 0); break;
	case N_ANY: emit(p, I_ANY, 0, 0); break;
	case N_SET: emit(p, I_SET, x.a, 0); break;
	case N_ASSERT: emit(p, I_ASSERT, x.a, 0); break;
	case N_BREF: emit(p, I_BREF, x.a, 0); break;
	case N_CAT:
		gen(ps, x.a);
		gen(ps, x.b);
		break;
	case N_ALT: {
		int sp = emit(p, I_SPLIT, 0, 0);
		p->code[sp].x = p->n;
		gen(ps, x.a);
		int j = emit(p, I_JMP, 0, 0);
		p->code[sp].y = p->n;
		gen(ps, x.b);
		p->code[j].x = p->n;
		break;
	}
	case N_GROUP:
		emit(p, I_SAVE, 2 * x.b, 0);
		gen(ps, x.a);
		emit(p, I_SAVE, 2 * x.b + 1, 0);
		break;
	case N_REP: {
		if (x.c < 0) {
			/* {m,}: m-1 copies, then the loop. With m == 0:
			 * ENTER; SPLIT body, out; body; CHECK (body, out); out.
			 * With m >= 1 the loop is entered at the body.
			 * CHECK loops again only after an iteration that consumed
			 * input, which also stops empty iterations after non-empty
			 * ones; a single empty iteration can still complete (so
			 * (a*)* sets \1 on an empty match). */
			for (int i = 1; i < x.b; i++)
				gen(ps, x.a);
			int id = p->nloops++;
			emit(p, I_ENTER, id, 0);
			int top = x.b ? -1 : emit(p, I_SPLIT, 0, 0);
			int body = p->n;
			if (top >= 0)
				p->code[top].x = body;
			gen(ps, x.a);
			int chk = emit(p, I_CHECK, body, 0);
			p->code[chk].z = id;
			p->code[chk].y = p->n;
			if (top >= 0)
				p->code[top].y = p->n;
		} else {
			for (int i = 0; i < x.b; i++)
				gen(ps, x.a);
			/* optional copies: SPLIT next, end; the pending splits are
			 * chained through y until the end is known */
			int chain = -1;
			for (int i = x.b; i < x.c; i++) {
				int sp = emit(p, I_SPLIT, 0, chain);
				p->code[sp].x = p->n;
				chain = sp;
				gen(ps, x.a);
			}
			while (chain >= 0) {
				int nx = p->code[chain].y;
				p->code[chain].y = p->n;
				chain = nx;
			}
		}
		break;
	}
	}
}

static void free_prog(struct prog *p)
{
	if (!p)
		return;
	for (int i = 0; i < p->nsets; i++)
		free(p->sets[i].r);
	free(p->sets);
	free(p->code);
	free(p);
}

int regcomp(regex_t *restrict re, const char *restrict pat, int flags)
{
	struct prog *p = calloc(1, sizeof *p);
	if (!p)
		return REG_ESPACE;
	struct parser ps = { (const unsigned char *)pat, strlen(pat), 0, flags & REG_EXTENDED, flags & REG_ICASE,
	                     flags & REG_NEWLINE, 0, 0, 0, p, 0, 0, 0, 0 };
	p->flags = flags;
	int root = parse_alt(&ps);
	if (root >= 0 && ps.pos < ps.len)
		ps.err = ps.ere ? REG_EPAREN : REG_EPAREN;
	if (!ps.err) {
		long sz = size_of(&ps, root) + 3;
		if (sz > MAX_INST || !(p->code = malloc((size_t)sz * sizeof *p->code)))
			ps.err = REG_ESPACE;
	}
	if (ps.err) {
		free(ps.nodes);
		free_prog(p);
		return ps.err;
	}
	emit(p, I_SAVE, 0, 0);
	gen(&ps, root);
	emit(p, I_SAVE, 1, 0);
	emit(p, I_MATCH, 0, 0);
	free(ps.nodes);
	p->nsub = ps.nsub;
	re->re_nsub = (size_t)ps.nsub;
	re->__prog = p;
	return 0;
}

void regfree(regex_t *re)
{
	free_prog(re->__prog);
	re->__prog = 0;
}

/* ---- matching ---- */

struct vm {
	const struct prog *p;
	const unsigned char *s; /* whole string (offsets are relative to it) */
	size_t beg, end;        /* the range being matched */
	int eflags, icase, newline;
	int ncap;
};

static int is_word(int c)
{
	return c >= 0 && c < BAD && (c == '_' || iswalnum((wint_t)c));
}

static int char_at(const struct vm *v, size_t pos, int *len)
{
	if (pos >= v->end) {
		*len = 0;
		return -1;
	}
	int c;
	*len = dec(v->s + pos, v->end - pos, &c);
	return c;
}

static int char_before(const struct vm *v, size_t pos)
{
	if (pos <= v->beg)
		return -1;
	size_t q = pos - 1;
	while (q > v->beg && pos - q < 4 && (v->s[q] & 0xc0) == 0x80)
		q--;
	int c, n = dec(v->s + q, pos - q, &c);
	if (q + (size_t)n != pos) /* not a complete character: the last byte */
		dec(v->s + pos - 1, 1, &c);
	return c;
}

static int assert_ok(const struct vm *v, int kind, size_t pos, int prev, int next)
{
	switch (kind) {
	case A_BOL:
		return (pos == v->beg && !(v->eflags & REG_NOTBOL)) || (v->newline && prev == '\n');
	case A_EOL:
		return (pos == v->end && !(v->eflags & REG_NOTEOL)) || (v->newline && next == '\n');
	case A_WORDB: return is_word(prev) != is_word(next);
	case A_NWORDB: return is_word(prev) == is_word(next);
	case A_WBEG: return !is_word(prev) && is_word(next);
	case A_WEND: return is_word(prev) && !is_word(next);
	case A_BUFBEG: return pos == v->beg;
	default: return pos == v->end;
	}
}

static int char_ok(const struct vm *v, const struct inst *in, int c)
{
	if (c < 0)
		return 0;
	switch (in->op) {
	case I_CHAR:
		return in->x == c || (v->icase && c < BAD && in->x == (int)towlower((wint_t)c));
	case I_ANY:
		return c < BAD && !(v->newline && c == '\n');
	default: {
		const struct set *s = &v->p->sets[in->x];
		if (c >= BAD)
			return 0;
		if (s->neg && v->newline && c == '\n')
			return 0;
		int r = in_set_raw(s, c);
		if (!r && v->icase)
			r = in_set_raw(s, (int)towlower((wint_t)c)) || in_set_raw(s, (int)towupper((wint_t)c));
		return r != s->neg;
	}
	}
}

/* -- Pike VM -- */

struct tlist {
	int n;
	int *pc;
	regoff_t *cap; /* n x ncap */
};

struct pike {
	struct vm *v;
	unsigned *mark;
	unsigned gen;
	regoff_t *work;
	struct { int pc, slot; regoff_t old; } *stack;
};

static void add_thread(struct pike *k, struct tlist *l, int pc0, const regoff_t *caps, size_t pos, int prev, int next)
{
	const struct prog *p = k->v->p;
	int ncap = k->v->ncap;
	memcpy(k->work, caps, (size_t)ncap * sizeof *caps);
	int sp = 0;
	k->stack[sp].pc = pc0;
	k->stack[sp++].slot = -1;
	while (sp) {
		sp--;
		if (k->stack[sp].slot >= 0) {
			k->work[k->stack[sp].slot] = k->stack[sp].old;
			continue;
		}
		int pc = k->stack[sp].pc;
		if (k->mark[pc] == k->gen)
			continue;
		k->mark[pc] = k->gen;
		const struct inst *in = &p->code[pc];
		switch (in->op) {
		case I_JMP:
			k->stack[sp].pc = in->x;
			k->stack[sp++].slot = -1;
			break;
		case I_SPLIT:
			k->stack[sp].pc = in->y;
			k->stack[sp++].slot = -1;
			k->stack[sp].pc = in->x;
			k->stack[sp++].slot = -1;
			break;
		case I_SAVE:
			if (in->x < ncap) {
				k->stack[sp].slot = in->x;
				k->stack[sp++].old = k->work[in->x];
				k->work[in->x] = (regoff_t)pos;
			}
			k->stack[sp].pc = pc + 1;
			k->stack[sp++].slot = -1;
			break;
		case I_ASSERT:
			if (assert_ok(k->v, in->x, pos, prev, next)) {
				k->stack[sp].pc = pc + 1;
				k->stack[sp++].slot = -1;
			}
			break;
		case I_ENTER:
			k->stack[sp].pc = pc + 1;
			k->stack[sp++].slot = -1;
			break;
		case I_CHECK: /* the marks stop empty iterations from looping */
			k->stack[sp].pc = in->y;
			k->stack[sp++].slot = -1;
			k->stack[sp].pc = in->x;
			k->stack[sp++].slot = -1;
			break;
		default:
			l->pc[l->n] = pc;
			memcpy(l->cap + (size_t)l->n * (size_t)ncap, k->work, (size_t)ncap * sizeof *caps);
			l->n++;
		}
	}
}

static int pike_run(struct vm *v, regoff_t *best)
{
	const struct prog *p = v->p;
	int ncap = v->ncap, n = p->n;
	size_t sz = (size_t)n * (size_t)ncap * sizeof(regoff_t);
	struct tlist a = { 0, malloc((size_t)n * sizeof(int)), malloc(sz) };
	struct tlist b = { 0, malloc((size_t)n * sizeof(int)), malloc(sz) };
	struct pike k = { v, calloc((size_t)n, sizeof(unsigned)), 0, malloc((size_t)ncap * sizeof(regoff_t)),
	                  malloc((size_t)(2 * n + 2) * sizeof *k.stack) };
	regoff_t *seed = malloc((size_t)ncap * sizeof *seed);
	int r = REG_ESPACE;
	if (!a.pc || !a.cap || !b.pc || !b.cap || !k.mark || !k.work || !k.stack || !seed)
		goto out;

	for (int i = 0; i < ncap; i++)
		seed[i] = -1;
	int matched = 0;
	struct tlist *cl = &a, *nl = &b;
	size_t pos = v->beg;
	int prev = char_before(v, pos), clen, c = char_at(v, pos, &clen);
	for (;;) {
		k.gen++;
		/* the threads already in cl were added under the previous
		 * generation; re-mark them so a new seed does not duplicate */
		for (int i = 0; i < cl->n; i++)
			k.mark[cl->pc[i]] = k.gen;
		if (!matched)
			add_thread(&k, cl, 0, seed, pos, prev, c);
		if (!cl->n && matched)
			break;
		int nlen;
		size_t npos = pos + (size_t)clen;
		int nc = clen ? char_at(v, npos, &nlen) : -1;
		k.gen++;
		nl->n = 0;
		for (int i = 0; i < cl->n; i++) {
			regoff_t *tc = cl->cap + (size_t)i * (size_t)ncap;
			const struct inst *in = &p->code[cl->pc[i]];
			if (matched && tc[0] > best[0])
				continue; /* started later than the match we have */
			if (in->op == I_MATCH) {
				if (!matched || tc[0] < best[0] || (tc[0] == best[0] && tc[1] > best[1])) {
					memcpy(best, tc, (size_t)ncap * sizeof *best);
					matched = 1;
				}
				continue;
			}
			if (clen && char_ok(v, in, c))
				add_thread(&k, nl, cl->pc[i] + 1, tc, npos, c, nc);
		}
		struct tlist *t = cl;
		cl = nl;
		nl = t;
		if (!clen)
			break;
		prev = c;
		c = nc;
		pos = npos;
		clen = c < 0 ? 0 : nlen;
	}
	r = matched ? 0 : REG_NOMATCH;
out:
	free(seed);
	free(a.pc);
	free(a.cap);
	free(b.pc);
	free(b.cap);
	free(k.mark);
	free(k.work);
	free(k.stack);
	return r;
}

/* -- backtracking, for back-references -- */

struct frame {
	int kind; /* 0 branch, 1 restore capture, 2 restore loop */
	int a;
	regoff_t b;
};

static int bt_run(struct vm *v, regoff_t *best)
{
	const struct prog *p = v->p;
	int ncap = 2 * (p->nsub + 1);
	regoff_t *cap = malloc((size_t)ncap * sizeof *cap);
	regoff_t *loops = malloc((size_t)(p->nloops + 1) * sizeof *loops);
	size_t scap = 1024, sp;
	struct frame *st = malloc(scap * sizeof *st);
	long steps = 0;
	int r = REG_NOMATCH;
	if (!cap || !loops || !st) {
		r = REG_ESPACE;
		goto out;
	}
	for (size_t start = v->beg; start <= v->end && r == REG_NOMATCH;) {
		for (int i = 0; i < ncap; i++)
			cap[i] = -1;
		for (int i = 0; i < p->nloops; i++)
			loops[i] = -1;
		int found = 0;
		sp = 0;
		st[sp++] = (struct frame){ 0, 0, (regoff_t)start };
		while (sp) {
			struct frame f = st[--sp];
			if (f.kind == 1) {
				cap[f.a] = f.b;
				continue;
			}
			if (f.kind == 2) {
				loops[f.a] = f.b;
				continue;
			}
			int pc = f.a;
			size_t pos = (size_t)f.b;
			for (;;) {
				if (++steps > BT_BUDGET) {
					r = REG_ESPACE;
					goto out;
				}
				if (sp + 2 >= scap) {
					if (scap >= (1u << 26)) {
						r = REG_ESPACE;
						goto out;
					}
					struct frame *ns = realloc(st, 2 * scap * sizeof *st);
					if (!ns) {
						r = REG_ESPACE;
						goto out;
					}
					st = ns;
					scap *= 2;
				}
				const struct inst *in = &p->code[pc];
				int len, c;
				if (in->op == I_CHAR || in->op == I_ANY || in->op == I_SET) {
					c = char_at(v, pos, &len);
					if (!char_ok(v, in, c))
						break;
					pos += (size_t)len;
					pc++;
				} else if (in->op == I_ASSERT) {
					int l2;
					if (!assert_ok(v, in->x, pos, char_before(v, pos), char_at(v, pos, &l2)))
						break;
					pc++;
				} else if (in->op == I_SPLIT) {
					st[sp++] = (struct frame){ 0, in->y, (regoff_t)pos };
					pc = in->x;
				} else if (in->op == I_JMP) {
					pc = in->x;
				} else if (in->op == I_SAVE) {
					st[sp++] = (struct frame){ 1, in->x, cap[in->x] };
					cap[in->x] = (regoff_t)pos;
					pc++;
				} else if (in->op == I_ENTER) {
					st[sp++] = (struct frame){ 2, in->x, loops[in->x] };
					loops[in->x] = (regoff_t)pos;
					pc++;
				} else if (in->op == I_CHECK) {
					if (loops[in->z] == (regoff_t)pos) {
						pc = in->y; /* no progress: leave the loop */
					} else {
						st[sp++] = (struct frame){ 0, in->y, (regoff_t)pos };
						st[sp++] = (struct frame){ 2, in->z, loops[in->z] };
						loops[in->z] = (regoff_t)pos;
						pc = in->x;
					}
				} else if (in->op == I_BREF) {
					regoff_t so = cap[2 * in->x], eo = cap[2 * in->x + 1];
					if (so < 0 || eo < 0)
						break;
					size_t q = (size_t)so, e = (size_t)eo;
					int ok = 1;
					while (q < e) {
						int c1, c2, l1, l2;
						l1 = dec(v->s + q, e - q, &c1);
						c2 = char_at(v, pos, &l2);
						if (c2 < 0 || (c1 != c2 && !(v->icase && c1 < BAD && c2 < BAD &&
						                             towlower((wint_t)c1) == towlower((wint_t)c2)))) {
							ok = 0;
							break;
						}
						q += (size_t)l1;
						pos += (size_t)l2;
					}
					if (!ok)
						break;
					pc++;
				} else { /* I_MATCH */
					if (!found || cap[1] > best[1]) {
						memcpy(best, cap, (size_t)ncap * sizeof *cap);
						found = 1;
					}
					break;
				}
			}
		}
		if (found) {
			r = 0;
			break;
		}
		if (start == v->end)
			break;
		int l;
		char_at(v, start, &l);
		start += (size_t)l;
	}
out:
	free(cap);
	free(loops);
	free(st);
	return r;
}

int regexec(const regex_t *restrict re, const char *restrict str, size_t nmatch, regmatch_t *restrict pm, int eflags)
{
	const struct prog *p = re->__prog;
	if (!p)
		return REG_BADPAT;
	struct vm v = { p, (const unsigned char *)str, 0, 0, eflags, p->flags & REG_ICASE, p->flags & REG_NEWLINE, 0 };
	if (eflags & REG_STARTEND) {
		if (!pm || pm[0].rm_so < 0 || pm[0].rm_eo < pm[0].rm_so)
			return REG_BADPAT;
		v.beg = (size_t)pm[0].rm_so;
		v.end = (size_t)pm[0].rm_eo;
	} else {
		v.end = strlen(str);
	}
	if (p->flags & REG_NOSUB)
		nmatch = 0;
	int ncap = 2 * (p->nsub + 1);
	v.ncap = p->backrefs ? ncap : (nmatch ? ncap : 2);
	regoff_t *best = malloc((size_t)ncap * sizeof *best);
	if (!best)
		return REG_ESPACE;
	for (int i = 0; i < ncap; i++)
		best[i] = -1;
	int r = p->backrefs ? bt_run(&v, best) : pike_run(&v, best);
	if (r == 0) {
		for (size_t i = 0; i < nmatch; i++) {
			if ((int)(2 * i + 1) < v.ncap && best[2 * i] >= 0 && best[2 * i + 1] >= 0) {
				pm[i].rm_so = best[2 * i];
				pm[i].rm_eo = best[2 * i + 1];
			} else {
				pm[i].rm_so = pm[i].rm_eo = -1;
			}
		}
	}
	free(best);
	return r;
}

size_t regerror(int e, const regex_t *restrict re, char *restrict buf, size_t size)
{
	static const char *const msgs[] = {
		"Success", "No match", "Invalid regular expression", "Invalid collation character",
		"Invalid character class name", "Trailing backslash", "Invalid back reference",
		"Unmatched [, [^, [:, [., or [=", "Unmatched ( or \\(", "Unmatched \\{",
		"Invalid content of \\{\\}", "Invalid range end", "Memory exhausted",
		"Invalid preceding regular expression",
	};
	(void)re;
	const char *m = e >= 0 && e < (int)(sizeof msgs / sizeof msgs[0]) ? msgs[e] : "Unknown error";
	size_t n = strlen(m) + 1;
	if (size) {
		size_t k = n < size ? n : size;
		memcpy(buf, m, k - 1);
		buf[k - 1] = 0;
	}
	return n;
}
