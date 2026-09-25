/* <search.h> and <uchar.h> */
#include <search.h>
#include <uchar.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

static int cmpint(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static int walked, last, sorted_ok, maxlevel;
static void visit(const void *n, VISIT v, int level)
{
	if (level > maxlevel)
		maxlevel = level;
	if (v == postorder || v == leaf) {
		int k = **(int *const *)n;
		if (walked && k <= last)
			sorted_ok = 0;
		last = k;
		walked++;
	}
}

static void trees(void)
{
	static int keys[4096];
	static char present[4096];
	void *root = 0;
	unsigned seed = 12345;
	int count = 0;
	for (int i = 0; i < 4096; i++)
		keys[i] = i;
	for (int it = 0; it < 40000; it++) {
		seed = seed * 1103515245 + 12345;
		int k = (int)(seed >> 8) % 4096;
		if ((seed >> 4) & 1) {
			int **r = tsearch(&keys[k], &root, cmpint);
			CHECK(r && **r == k);
			count += !present[k];
			present[k] = 1;
		} else {
			void *r = tdelete(&keys[k], &root, cmpint);
			CHECK(!r == !present[k]);
			count -= present[k];
			present[k] = 0;
		}
		if (it % 997 == 0) {
			walked = maxlevel = 0;
			sorted_ok = 1;
			twalk(root, visit);
			CHECK(walked == count && sorted_ok);
			/* AVL: height <= 1.44 log2(n + 2) */
			CHECK(maxlevel < 20);
		}
	}
	for (int k = 0; k < 4096; k++)
		CHECK(!tfind(&keys[k], &root, cmpint) == !present[k]);
	tdestroy(root, 0);
}

static void hashes(void)
{
	CHECK(hcreate(100));
	CHECK(!hcreate(10)); /* only one global table */
	char *names[] = { "alpha", "beta", "gamma", "delta", "" };
	for (int i = 0; i < 5; i++) {
		ENTRY e = { names[i], (void *)(long)(i + 1) };
		CHECK(hsearch(e, ENTER) != 0);
	}
	ENTRY q = { "gamma", 0 };
	ENTRY *r = hsearch(q, FIND);
	CHECK(r && (long)r->data == 3);
	q.key = "epsilon";
	CHECK(!hsearch(q, FIND) && errno == ESRCH);
	q.key = "";
	CHECK(hsearch(q, FIND) && (long)hsearch(q, FIND)->data == 5);
	hdestroy();

	struct hsearch_data d = { 0 };
	CHECK(hcreate_r(4, &d));
	static char buf[64][8];
	int full = 0;
	for (int i = 0; i < 64; i++) {
		buf[i][0] = (char)('a' + i % 26);
		buf[i][1] = (char)('0' + i / 26);
		ENTRY e = { buf[i], 0 }, *out;
		if (!hsearch_r(e, ENTER, &out, &d)) {
			CHECK(errno == ENOMEM);
			full = 1;
			break;
		}
	}
	CHECK(full); /* fixed capacity, reported rather than overflowing */
	hdestroy_r(&d);
}

static void linear(void)
{
	int a[8] = { 5, 3, 9 };
	size_t n = 3;
	int k = 9, m = 7;
	CHECK(lfind(&k, a, &n, sizeof(int), cmpint) == &a[2]);
	CHECK(!lfind(&m, a, &n, sizeof(int), cmpint));
	CHECK(lsearch(&m, a, &n, sizeof(int), cmpint) == &a[3] && n == 4 && a[3] == 7);
	struct q { struct q *next, *prev; int v; } x = { 0, 0, 1 }, y = { 0, 0, 2 }, z = { 0, 0, 3 };
	insque(&x, 0);
	insque(&z, &x);
	insque(&y, &x);
	CHECK(x.next == &y && y.next == &z && z.prev == &y && y.prev == &x);
	remque(&y);
	CHECK(x.next == &z && z.prev == &x);
}

static void uchars(void)
{
	mbstate_t st = { 0 };
	char16_t c16;
	const char *s = "a\xc3\xa9\xf0\x9f\x98\x80";
	CHECK(mbrtoc16(&c16, s, 8, &st) == 1 && c16 == 'a');
	CHECK(mbrtoc16(&c16, s + 1, 8, &st) == 2 && c16 == 0xe9);
	CHECK(mbrtoc16(&c16, s + 3, 8, &st) == 4 && c16 == 0xd83d);
	CHECK(mbrtoc16(&c16, s + 7, 1, &st) == (size_t)-3 && c16 == 0xde00);
	char out[8];
	mbstate_t w = { 0 };
	CHECK(c16rtomb(out, 0xd83d, &w) == 0);
	CHECK(c16rtomb(out, 0xde00, &w) == 4 && !memcmp(out, "\xf0\x9f\x98\x80", 4));
	CHECK(c16rtomb(out, 0xdc00, &w) == (size_t)-1 && errno == EILSEQ);
	char32_t c32;
	memset(&st, 0, sizeof st);
	CHECK(mbrtoc32(&c32, "\xe2\x82", 2, &st) == (size_t)-2);
	CHECK(mbrtoc32(&c32, "\xac", 1, &st) == 1 && c32 == 0x20ac);
	CHECK(c32rtomb(out, 0x20ac, 0) == 3 && !memcmp(out, "\xe2\x82\xac", 3));
	CHECK(c32rtomb(out, 0xd800, 0) == (size_t)-1);
	CHECK(c32rtomb(out, 0x110000, 0) == (size_t)-1);
}

int main(void)
{
	trees();
	hashes();
	linear();
	uchars();
	return t_done();
}
