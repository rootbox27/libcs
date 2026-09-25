/* %n is refused: a format containing it terminates the process. */
#include <stdio.h>

#pragma GCC diagnostic ignored "-Wformat"

int main(int argc, char **argv)
{
	int n;
	char b[32];
	snprintf(b, sizeof b, argc > 0 ? "abc%n" : "", &n);
	return 0;
}
