/* Message catalogues: there are none, so catgets returns its default. */
#include <nl_types.h>
#include <errno.h>

nl_catd catopen(const char *name, int flag)
{
	(void)name;
	(void)flag;
	errno = ENOENT;
	return (nl_catd)-1;
}

char *catgets(nl_catd cat, int set, int num, const char *def)
{
	(void)cat;
	(void)set;
	(void)num;
	errno = EBADF;
	return (char *)def;
}

int catclose(nl_catd cat)
{
	(void)cat;
	errno = EBADF;
	return -1;
}
