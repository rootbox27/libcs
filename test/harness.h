/* Minimal test harness. No stdio exists yet, so output goes straight to
 * write(2). A test's main() ends with `return t_done();`. */
#ifndef HARNESS_H
#define HARNESS_H
#include <string.h>
#include <unistd.h>

static int t_failures;

static void t_puts(const char *s)
{
	write(2, s, strlen(s));
}

static void t_putl(long v)
{
	char b[24];
	int i = sizeof b;
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	do b[--i] = (char)('0' + u % 10); while (u /= 10);
	if (v < 0)
		b[--i] = '-';
	write(2, b + i, sizeof b - i);
}

static void t_fail(const char *file, int line, const char *expr)
{
	t_failures++;
	t_puts(file);
	t_puts(":");
	t_putl(line);
	t_puts(": FAIL: ");
	t_puts(expr);
	t_puts("\n");
}

#define CHECK(e) ((e) ? (void)0 : t_fail(__FILE__, __LINE__, #e))
#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)
/* Sign of a comparison result, for functions that only promise a sign. */
#define SGN(x) (((x) > 0) - ((x) < 0))

/* Exit status: 0 if every check passed. (Returning the raw count would
 * wrap modulo 256.) */
static __attribute__((__unused__)) int t_done(void)
{
	return t_failures != 0;
}

#endif
