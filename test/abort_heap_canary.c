/* Overflowing a small allocation by one byte is caught by the chunk
 * canary when the allocation is freed. */
#include <stdlib.h>

int main(int argc, char **argv)
{
	size_t n = 20 + (size_t)argc - 3; /* 20, at run time */
	volatile char *p = malloc(n);
	p[n] = 'x';
	free((void *)p);
	return 0;
}
