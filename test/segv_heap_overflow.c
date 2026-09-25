/* Writing one byte past the end of an allocation must fault. */
#include <stdlib.h>

int main(int argc, char **argv)
{
	/* A runtime size and volatile store so the compiler neither drops
	 * the write nor flags the (intentional) overflow. */
	size_t n = 4096 + (size_t)argc - 3;
	volatile char *p = malloc(n);
	p[n] = 1;
	return 0;
}
