/* A fortified strcpy into a too-small buffer must abort. */
#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 2
#include <string.h>

int main(int argc, char **argv)
{
	char buf[4];
	/* argv[1] is "alpha": 6 bytes into a 4-byte buffer */
	strcpy(buf, argv[1]);
	return buf[0];
}
