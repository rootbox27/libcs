/* realpath: resolve "." and ".." components and symbolic links without
 * relying on /proc. Every component must exist. */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXLINKS 40

char *realpath(const char *__restrict path, char *__restrict resolved)
{
	char out[PATH_MAX];       /* resolved prefix, always absolute */
	char rest[PATH_MAX];      /* what is left to resolve */
	char link[PATH_MAX];
	size_t ol, links = 0;

	if (!path) {
		errno = EINVAL;
		return 0;
	}
	if (!*path) {
		errno = ENOENT;
		return 0;
	}
	if (strlcpy(rest, path, sizeof rest) >= sizeof rest) {
		errno = ENAMETOOLONG;
		return 0;
	}
	if (rest[0] == '/') {
		out[0] = '/';
		ol = 1;
	} else {
		if (!getcwd(out, sizeof out))
			return 0;
		ol = strlen(out);
	}
	out[ol] = 0;

	char *p = rest;
	while (*p) {
		/* next component */
		while (*p == '/')
			p++;
		if (!*p)
			break;
		char *e = strchrnul(p, '/');
		size_t cl = (size_t)(e - p);
		if (cl == 1 && p[0] == '.') {
			p = e;
			continue;
		}
		if (cl == 2 && p[0] == '.' && p[1] == '.') {
			while (ol > 1 && out[ol - 1] != '/')
				ol--;
			if (ol > 1)
				ol--;
			out[ol] = 0;
			p = e;
			continue;
		}
		size_t need = ol + (ol > 1) + cl;
		if (need >= sizeof out) {
			errno = ENAMETOOLONG;
			return 0;
		}
		size_t saved = ol;
		if (ol > 1)
			out[ol++] = '/';
		memcpy(out + ol, p, cl);
		ol += cl;
		out[ol] = 0;
		p = e;

		ssize_t k = readlink(out, link, sizeof link - 1);
		if (k < 0) {
			if (errno == EINVAL)
				continue; /* exists and is not a symlink */
			return 0;
		}
		if (++links > MAXLINKS) {
			errno = ELOOP;
			return 0;
		}
		link[k] = 0;
		/* splice: link target followed by the unresolved rest */
		size_t rl = strlen(p);
		if ((size_t)k + rl + 1 > sizeof rest) {
			errno = ENAMETOOLONG;
			return 0;
		}
		memmove(rest + k, p, rl + 1);
		memcpy(rest, link, (size_t)k);
		p = rest;
		if (link[0] == '/') {
			ol = 1;
		} else {
			ol = saved;
		}
		out[ol] = 0;
	}
	/* a trailing slash requires a directory */
	size_t pl = strlen(path);
	if (pl && path[pl - 1] == '/' && access(out, F_OK) == 0) {
		char probe[PATH_MAX + 2];
		memcpy(probe, out, ol);
		memcpy(probe + ol, "/.", 3);
		if (access(probe, F_OK))
			return 0;
	}
	if (resolved) {
		memcpy(resolved, out, ol + 1);
		return resolved;
	}
	return strdup(out);
}
