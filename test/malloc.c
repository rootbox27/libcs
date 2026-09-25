#include "harness.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	/* volatile keeps the compiler from rejecting the huge sizes itself */
	volatile size_t big = SIZE_MAX, half = SIZE_MAX / 2, bad_align = 24;
	char *p = malloc(100);
	CHECK(p && (uintptr_t)p % 16 == 0);
	memset(p, 'a', 100);
	p = realloc(p, 5000);
	CHECK(p && p[0] == 'a' && p[99] == 'a');
	memset(p, 'b', 5000);
	p = realloc(p, 10);
	CHECK(p && p[9] == 'b');
	free(p);
	free(0);

	void *z = malloc(0);
	CHECK(z != 0);
	free(z);

	int *c = calloc(1000, sizeof(int));
	CHECK(c != 0);
	int nz = 0;
	for (int i = 0; i < 1000; i++)
		nz |= c[i];
	CHECK(nz == 0);
	free(c);

	errno = 0;
	CHECK(calloc(half, 3) == 0 && errno == ENOMEM);
	errno = 0;
	CHECK(malloc(big) == 0 && errno == ENOMEM);
	errno = 0;
	CHECK(reallocarray(0, half, 3) == 0 && errno == ENOMEM);
	p = reallocarray(0, 10, 10);
	CHECK(p != 0);
	freezero(p, 100);

	for (size_t a = 16; a <= 65536; a <<= 1) {
		void *q = aligned_alloc(a, 24);
		CHECK(q && (uintptr_t)q % a == 0);
		free(q);
		q = 0;
		CHECK(posix_memalign(&q, a, 3) == 0 && (uintptr_t)q % a == 0);
		free(q);
	}
	void *q = (void *)1;
	errno = 0;
	CHECK(posix_memalign(&q, 3, 8) == EINVAL && q == (void *)1 && errno == 0);
	errno = 0;
	CHECK(aligned_alloc(bad_align, 8) == 0 && errno == EINVAL);
	return t_done();
}
