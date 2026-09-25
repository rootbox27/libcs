/* A double free must abort. */
#include <stdlib.h>

int main(void)
{
	char *volatile p = malloc(40);
	free(p);
	free(p);
	return 0;
}
