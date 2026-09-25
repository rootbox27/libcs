/* Overwriting a canary-protected frame must abort on return. */
#include <string.h>

static __attribute__((__noinline__)) int smash(const char *s, size_t n)
{
	char buf[16];
	char *volatile p = buf; /* hide the overflow from fortify */
	memset(p, 'A', n);
	return p[0] + s[0];
}

int main(int argc, char **argv)
{
	return smash(argv[1], 64);
}
