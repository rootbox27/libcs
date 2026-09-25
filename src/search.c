/* <search.h>: hash table (keyed hash, so adversarial keys cannot force
 * collisions), AVL trees for tsearch, linear search and queues. */
#include <search.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- hsearch ---- */

struct __htab {
	ENTRY *e;
	size_t mask, used;
	uint64_t k0, k1;
};

static uint64_t rotl(uint64_t x, int b) { return x << b | x >> (64 - b); }

/* SipHash-1-3 of a NUL-terminated string */
static uint64_t keyhash(const struct __htab *t, const char *s)
{
	uint64_t v0 = t->k0 ^ 0x736f6d6570736575ULL, v1 = t->k1 ^ 0x646f72616e646f6dULL;
	uint64_t v2 = t->k0 ^ 0x6c7967656e657261ULL, v3 = t->k1 ^ 0x7465646279746573ULL;
	size_t n = strlen(s), i = 0;
#define SIPROUND do { \
		v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32); \
		v2 += v3; v3 = rotl(v3, 16); v3 ^= v2; \
		v0 += v3; v3 = rotl(v3, 21); v3 ^= v0; \
		v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32); \
	} while (0)
	for (; i + 8 <= n; i += 8) {
		uint64_t m;
		memcpy(&m, s + i, 8);
		v3 ^= m;
		SIPROUND;
		v0 ^= m;
	}
	uint64_t b = (uint64_t)n << 56;
	for (size_t j = 0; i + j < n; j++)
		b |= (uint64_t)(unsigned char)s[i + j] << (8 * j);
	v3 ^= b;
	SIPROUND;
	v0 ^= b;
	v2 ^= 0xff;
	SIPROUND;
	SIPROUND;
	SIPROUND;
#undef SIPROUND
	return v0 ^ v1 ^ v2 ^ v3;
}

int hcreate_r(size_t nel, struct hsearch_data *d)
{
	if (nel > (SIZE_MAX >> 3)) {
		errno = ENOMEM;
		return 0;
	}
	size_t size = 8;
	while (size < nel + nel / 3 + 1)
		size *= 2;
	struct __htab *t = malloc(sizeof *t);
	if (!t || !(t->e = calloc(size, sizeof *t->e))) {
		free(t);
		errno = ENOMEM;
		return 0;
	}
	t->mask = size - 1;
	t->used = 0;
	arc4random_buf(&t->k0, sizeof t->k0);
	arc4random_buf(&t->k1, sizeof t->k1);
	d->__tab = t;
	return 1;
}

void hdestroy_r(struct hsearch_data *d)
{
	if (d && d->__tab) {
		free(d->__tab->e);
		free(d->__tab);
		d->__tab = 0;
	}
}

int hsearch_r(ENTRY item, ACTION act, ENTRY **ret, struct hsearch_data *d)
{
	struct __htab *t = d->__tab;
	if (!t) {
		*ret = 0;
		errno = ESRCH;
		return 0;
	}
	size_t i = (size_t)keyhash(t, item.key) & t->mask;
	for (size_t step = 1; t->e[i].key; i = (i + step++) & t->mask) {
		if (!strcmp(t->e[i].key, item.key)) {
			*ret = &t->e[i];
			return 1;
		}
	}
	/* keep a quarter free so probes stay short and terminate */
	if (act == FIND || t->used + 1 > t->mask - t->mask / 4) {
		*ret = 0;
		errno = act == FIND ? ESRCH : ENOMEM;
		return 0;
	}
	t->e[i] = item;
	t->used++;
	*ret = &t->e[i];
	return 1;
}

static struct hsearch_data global;

int hcreate(size_t nel)
{
	if (global.__tab)
		return 0;
	return hcreate_r(nel, &global);
}

void hdestroy(void) { hdestroy_r(&global); }

ENTRY *hsearch(ENTRY item, ACTION act)
{
	ENTRY *r;
	hsearch_r(item, act, &r, &global);
	return r;
}

/* ---- tsearch: AVL tree ---- */

struct node {
	const void *key; /* first: the result points at the key pointer */
	struct node *a[2];
	int h;
};

#define MAXH 100

static int height(struct node *n) { return n ? n->h : 0; }

static struct node *rot(struct node *n, int dir)
{
	/* rotate so the child on side !dir becomes the root */
	struct node *c = n->a[!dir];
	n->a[!dir] = c->a[dir];
	c->a[dir] = n;
	int hl = height(n->a[0]), hr = height(n->a[1]);
	n->h = 1 + (hl > hr ? hl : hr);
	hl = height(c->a[0]);
	hr = height(c->a[1]);
	c->h = 1 + (hl > hr ? hl : hr);
	return c;
}

static struct node *balance(struct node *n)
{
	int hl = height(n->a[0]), hr = height(n->a[1]);
	if (hl - hr > 1) {
		struct node *c = n->a[0];
		if (height(c->a[1]) > height(c->a[0]))
			n->a[0] = rot(c, 0);
		return rot(n, 1);
	}
	if (hr - hl > 1) {
		struct node *c = n->a[1];
		if (height(c->a[0]) > height(c->a[1]))
			n->a[1] = rot(c, 1);
		return rot(n, 0);
	}
	n->h = 1 + (hl > hr ? hl : hr);
	return n;
}

void *tfind(const void *key, void *const *rootp, int (*cmp)(const void *, const void *))
{
	if (!rootp)
		return 0;
	struct node *n = *rootp;
	while (n) {
		int c = cmp(key, n->key);
		if (!c)
			return n;
		n = n->a[c > 0];
	}
	return 0;
}

void *tsearch(const void *key, void **rootp, int (*cmp)(const void *, const void *))
{
	if (!rootp)
		return 0;
	struct node **path[MAXH], *found = 0;
	int depth = 0;
	struct node **pp = (struct node **)rootp;
	while (*pp) {
		int c = cmp(key, (*pp)->key);
		if (!c)
			return *pp;
		path[depth++] = pp;
		pp = &(*pp)->a[c > 0];
	}
	struct node *n = malloc(sizeof *n);
	if (!n)
		return 0;
	n->key = key;
	n->a[0] = n->a[1] = 0;
	n->h = 1;
	*pp = found = n;
	while (depth--)
		*path[depth] = balance(*path[depth]);
	return found;
}

void *tdelete(const void *restrict key, void **restrict rootp, int (*cmp)(const void *, const void *))
{
	if (!rootp || !*rootp)
		return 0;
	struct node **path[MAXH + 1];
	int depth = 0;
	struct node **pp = (struct node **)rootp;
	/* parent of the deleted node (POSIX: returned; any non-null value for
	 * the root) */
	void *parent = (void *)rootp;
	for (;;) {
		if (!*pp)
			return 0;
		int c = cmp(key, (*pp)->key);
		if (!c)
			break;
		parent = *pp;
		path[depth++] = pp;
		pp = &(*pp)->a[c > 0];
	}
	struct node *del = *pp;
	if (!del->a[0] || !del->a[1]) {
		*pp = del->a[!del->a[0]];
	} else {
		/* replace with the in-order successor */
		path[depth++] = pp;
		int succ_at = depth;
		struct node **sp = &del->a[1];
		while ((*sp)->a[0]) {
			path[depth++] = sp;
			sp = &(*sp)->a[0];
		}
		struct node *s = *sp;
		*sp = s->a[1];
		s->a[0] = del->a[0];
		s->a[1] = del->a[1];
		*pp = s;
		/* the successor now sits where del was: fix the saved slot */
		if (depth > succ_at)
			path[succ_at] = &s->a[1];
	}
	free(del);
	while (depth--)
		if (*path[depth])
			*path[depth] = balance(*path[depth]);
	return parent;
}

static void walk(const struct node *n, void (*act)(const void *, VISIT, int), int level)
{
	if (!n)
		return;
	if (!n->a[0] && !n->a[1]) {
		act(n, leaf, level);
		return;
	}
	act(n, preorder, level);
	walk(n->a[0], act, level + 1);
	act(n, postorder, level);
	walk(n->a[1], act, level + 1);
	act(n, endorder, level);
}

void twalk(const void *root, void (*act)(const void *, VISIT, int))
{
	if (act)
		walk(root, act, 0);
}

void tdestroy(void *root, void (*freekey)(void *))
{
	struct node *n = root;
	if (!n)
		return;
	tdestroy(n->a[0], freekey);
	tdestroy(n->a[1], freekey);
	if (freekey)
		freekey((void *)n->key);
	free(n);
}

/* ---- linear search ---- */

void *lfind(const void *key, const void *base, size_t *nel, size_t width, int (*cmp)(const void *, const void *))
{
	const char *p = base;
	for (size_t i = 0; i < *nel; i++, p += width)
		if (!cmp(key, p))
			return (void *)p;
	return 0;
}

void *lsearch(const void *key, void *base, size_t *nel, size_t width, int (*cmp)(const void *, const void *))
{
	void *r = lfind(key, base, nel, width, cmp);
	if (r)
		return r;
	r = (char *)base + *nel * width;
	memcpy(r, key, width);
	++*nel;
	return r;
}

/* ---- queues ---- */

struct qelem {
	struct qelem *next, *prev;
};

void insque(void *elem, void *pred)
{
	struct qelem *e = elem, *p = pred;
	if (!p) {
		e->next = e->prev = 0;
		return;
	}
	e->next = p->next;
	e->prev = p;
	p->next = e;
	if (e->next)
		e->next->prev = e;
}

void remque(void *elem)
{
	struct qelem *e = elem;
	if (e->next)
		e->next->prev = e->prev;
	if (e->prev)
		e->prev->next = e->next;
}
