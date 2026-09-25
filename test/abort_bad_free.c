/* free() of a pointer that malloc did not return must abort. */
#include <stdlib.h>

int main(void)
{
	char *p = malloc(64);
	free(p + 16);
	return 0;
}
