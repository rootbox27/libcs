/* A fortified sprintf into a too-small buffer must abort. */
#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 2
#include <stdio.h>

int main(int argc, char **argv)
{
	char buf[4];
	sprintf(buf, "%s", argv[1]); /* "alpha" */
	return buf[0];
}
