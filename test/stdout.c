/* Standard output: buffered data must be flushed at exit, after atexit
 * handlers run. Checked against test/stdout.expected by run.sh. */
#include <stdio.h>
#include <stdlib.h>

static void late(void)
{
	printf("from atexit\n");
}

int main(int argc, char **argv)
{
	atexit(late);
	printf("%s %d %.2f\n", "printf", 42, 2.5);
	puts("puts");
	fputs("fputs ", stdout);
	putchar('c');
	putchar('\n');
	fwrite("fwrite\n", 1, 7, stdout);
	fprintf(stderr, "stderr is not captured\n");
	for (int i = 0; i < 3000; i++)
		putchar(i % 26 + 'a');
	putchar('\n');
	printf("no newline at exit");
	return 0;
}
