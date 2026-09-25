/* Temporary file and directory creation. Names use kernel randomness;
 * files are created with O_EXCL | O_NOFOLLOW and mode 0600, directories
 * with mode 0700. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._";

/* Find the six X's that end at `end`; fail with EINVAL otherwise. */
static char *xs(char *tmpl, size_t suffix)
{
	size_t l = strlen(tmpl);
	if (l < 6 + suffix || memcmp(tmpl + l - suffix - 6, "XXXXXX", 6)) {
		errno = EINVAL;
		return 0;
	}
	return tmpl + l - suffix - 6;
}

static void fill(char *x)
{
	unsigned char r[6];
	__secure_random(r, sizeof r);
	for (int i = 0; i < 6; i++)
		x[i] = chars[r[i] & 63];
}

int mkostemps(char *tmpl, int suffix, int flags)
{
	char *x = xs(tmpl, suffix < 0 ? 0 : (size_t)suffix);
	if (!x || suffix < 0 || (flags & ~(O_APPEND | O_CLOEXEC | O_SYNC | O_DSYNC | O_NOATIME))) {
		errno = EINVAL;
		return -1;
	}
	for (int tries = 0; tries < 100; tries++) {
		fill(x);
		int fd = open(tmpl, flags | O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			break;
	}
	memcpy(x, "XXXXXX", 6);
	return -1;
}

int mkostemp(char *tmpl, int flags) { return mkostemps(tmpl, 0, flags); }
int mkstemps(char *tmpl, int suffix) { return mkostemps(tmpl, suffix, 0); }
int mkstemp(char *tmpl) { return mkostemps(tmpl, 0, 0); }

char *mkdtemp(char *tmpl)
{
	char *x = xs(tmpl, 0);
	if (!x)
		return 0;
	for (int tries = 0; tries < 100; tries++) {
		fill(x);
		if (!mkdir(tmpl, 0700))
			return tmpl;
		if (errno != EEXIST)
			break;
	}
	memcpy(x, "XXXXXX", 6);
	return 0;
}
