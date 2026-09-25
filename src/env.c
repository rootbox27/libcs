/* Environment: getenv, secure_getenv, setenv, unsetenv, putenv, clearenv.
 *
 * Strings that setenv allocates are remembered so they can be freed when
 * replaced or removed; strings from the initial environment or putenv are
 * never freed. Not thread-safe with respect to concurrent modification,
 * as POSIX allows. */
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char **env_alloc;        /* environ array owned by us, if any */
static size_t env_cap;
static char **owned;            /* strings we allocated */
static size_t owned_n, owned_cap;

static size_t name_len(const char *s)
{
	const char *e = strchrnul(s, '=');
	return (size_t)(e - s);
}

char *getenv(const char *name)
{
	size_t l = name_len(name);
	if (!l || name[l] || !__environ)
		return 0;
	for (char **e = __environ; *e; e++)
		if (!strncmp(name, *e, l) && (*e)[l] == '=')
			return *e + l + 1;
	return 0;
}

char *secure_getenv(const char *name)
{
	return __libc.secure ? 0 : getenv(name);
}

static int is_owned(const char *s, size_t *idx)
{
	for (size_t i = 0; i < owned_n; i++)
		if (owned[i] == s) {
			if (idx)
				*idx = i;
			return 1;
		}
	return 0;
}

static void release(char *s)
{
	size_t i;
	if (s && is_owned(s, &i)) {
		owned[i] = owned[--owned_n];
		free(s);
	}
}

/* Replace or add a "name=value" string (whose name has length l).
 * Returns the replaced string, or s itself if newly added. */
static int env_put(char *s, size_t l, int overwrite, char **old)
{
	size_t n = 0;
	*old = 0;
	if (__environ) {
		for (char **e = __environ; *e; e++, n++) {
			if (!strncmp(s, *e, l) && (*e)[l] == '=') {
				if (overwrite) {
					*old = *e;
					*e = s;
				}
				return 0;
			}
		}
	}
	/* append; the array needs room for n + 1 entries and a terminator */
	if (__environ != env_alloc || n + 2 > env_cap) {
		size_t cap = n + 2 > 16 ? (n + 2) * 2 : 16;
		char **ne = reallocarray(__environ == env_alloc ? env_alloc : 0, cap, sizeof *ne);
		if (!ne)
			return -1;
		if (__environ != env_alloc && n)
			memcpy(ne, __environ, n * sizeof *ne);
		env_alloc = ne;
		env_cap = cap;
		__environ = ne;
	}
	__environ[n] = s;
	__environ[n + 1] = 0;
	return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
	size_t l = name_len(name);
	if (!l || name[l]) {
		errno = EINVAL;
		return -1;
	}
	if (!overwrite && getenv(name))
		return 0;
	size_t vl = strlen(value);
	char *s = malloc(l + vl + 2);
	if (!s)
		return -1;
	memcpy(s, name, l);
	s[l] = '=';
	memcpy(s + l + 1, value, vl + 1);
	if (owned_n == owned_cap) {
		size_t cap = owned_cap ? owned_cap * 2 : 16;
		char **no = reallocarray(owned, cap, sizeof *no);
		if (!no) {
			free(s);
			return -1;
		}
		owned = no;
		owned_cap = cap;
	}
	char *old;
	if (env_put(s, l, 1, &old)) {
		free(s);
		return -1;
	}
	owned[owned_n++] = s;
	release(old);
	return 0;
}

int unsetenv(const char *name)
{
	size_t l = name_len(name);
	if (!l || name[l]) {
		errno = EINVAL;
		return -1;
	}
	if (!__environ)
		return 0;
	char **w = __environ;
	for (char **e = __environ; *e; e++) {
		if (!strncmp(name, *e, l) && (*e)[l] == '=')
			release(*e);
		else
			*w++ = *e;
	}
	*w = 0;
	return 0;
}

int putenv(char *s)
{
	size_t l = name_len(s);
	if (!s[l])
		return unsetenv(s);
	if (!l) {
		errno = EINVAL;
		return -1;
	}
	char *old;
	if (env_put(s, l, 1, &old)) {
		errno = ENOMEM;
		return -1;
	}
	release(old);
	return 0;
}

int clearenv(void)
{
	if (__environ)
		for (char **e = __environ; *e; e++)
			release(*e);
	free(env_alloc);
	env_alloc = 0;
	env_cap = 0;
	__environ = 0;
	return 0;
}

const char *getprogname(void)
{
	return __libc.progname;
}

void setprogname(const char *name)
{
	const char *p = strrchr(name, '/');
	__libc.progname = p ? p + 1 : name;
}
