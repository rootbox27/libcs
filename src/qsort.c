/* qsort, qsort_r and bsearch.
 *
 * qsort is introsort: median-of-three quicksort that switches to heapsort
 * when recursion gets too deep, and insertion sort for short runs. The
 * worst case is O(n log n), so hostile input cannot make it quadratic,
 * and it never allocates. */
#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef int (*cmp_r)(const void *, const void *, void *);

struct ctx {
	size_t w;
	cmp_r cmp;
	void *arg;
};

static void swap(char *a, char *b, size_t w)
{
	if (a == b)
		return;
	if (w % sizeof(long) == 0 && (uintptr_t)a % sizeof(long) == 0 && (uintptr_t)b % sizeof(long) == 0) {
		for (long *x = (long *)a, *y = (long *)b; w; w -= sizeof(long), x++, y++) {
			long t = *x;
			*x = *y;
			*y = t;
		}
		return;
	}
	char tmp[256];
	while (w) {
		size_t k = w < sizeof tmp ? w : sizeof tmp;
		memcpy(tmp, a, k);
		memcpy(a, b, k);
		memcpy(b, tmp, k);
		a += k;
		b += k;
		w -= k;
	}
}

#define CMP(c, a, b) ((c)->cmp((a), (b), (c)->arg))

static void insertion(char *base, size_t n, const struct ctx *c)
{
	for (size_t i = 1; i < n; i++)
		for (size_t j = i; j && CMP(c, base + (j - 1) * c->w, base + j * c->w) > 0; j--)
			swap(base + (j - 1) * c->w, base + j * c->w, c->w);
}

static void sift(char *base, size_t root, size_t n, const struct ctx *c)
{
	for (;;) {
		size_t child = 2 * root + 1;
		if (child >= n)
			return;
		if (child + 1 < n && CMP(c, base + child * c->w, base + (child + 1) * c->w) < 0)
			child++;
		if (CMP(c, base + root * c->w, base + child * c->w) >= 0)
			return;
		swap(base + root * c->w, base + child * c->w, c->w);
		root = child;
	}
}

static void heapsort_(char *base, size_t n, const struct ctx *c)
{
	for (size_t i = n / 2; i-- > 0;)
		sift(base, i, n, c);
	for (size_t end = n; end-- > 1;) {
		swap(base, base + end * c->w, c->w);
		sift(base, 0, end, c);
	}
}

static void introsort(char *base, size_t n, int depth, const struct ctx *c)
{
	size_t w = c->w;
	while (n > 16) {
		if (!depth--) {
			heapsort_(base, n, c);
			return;
		}
		/* median of three into position 0 */
		char *lo = base, *mid = base + (n / 2) * w, *hi = base + (n - 1) * w;
		if (CMP(c, mid, lo) < 0)
			swap(mid, lo, w);
		if (CMP(c, hi, mid) < 0) {
			swap(hi, mid, w);
			if (CMP(c, mid, lo) < 0)
				swap(mid, lo, w);
		}
		swap(base, mid, w);
		/* Hoare partition around base[0] */
		size_t i = 0, j = n;
		for (;;) {
			do i++; while (i < n && CMP(c, base + i * w, base) < 0);
			do j--; while (CMP(c, base + j * w, base) > 0);
			if (i >= j)
				break;
			swap(base + i * w, base + j * w, w);
		}
		swap(base, base + j * w, w);
		/* recurse into the smaller side, loop on the larger */
		size_t left = j, right = n - j - 1;
		if (left < right) {
			introsort(base, left, depth, c);
			base += (j + 1) * w;
			n = right;
		} else {
			introsort(base + (j + 1) * w, right, depth, c);
			n = left;
		}
	}
	insertion(base, n, c);
}

void qsort_r(void *base, size_t n, size_t w, cmp_r cmp, void *arg)
{
	if (n < 2 || !w)
		return;
	struct ctx c = { w, cmp, arg };
	int depth = 2 * (64 - __builtin_clzl(n));
	introsort(base, n, depth, &c);
}

static int call_plain(const void *a, const void *b, void *f)
{
	return ((int (*)(const void *, const void *))f)(a, b);
}

void qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *))
{
	qsort_r(base, n, w, call_plain, (void *)cmp);
}

void *bsearch(const void *key, const void *base, size_t n, size_t w, int (*cmp)(const void *, const void *))
{
	const char *b = base;
	while (n) {
		const char *mid = b + (n / 2) * w;
		int r = cmp(key, mid);
		if (!r)
			return (void *)mid;
		if (r > 0) {
			b = mid + w;
			n -= n / 2 + 1;
		} else {
			n /= 2;
		}
	}
	return 0;
}
