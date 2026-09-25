/* POSIX basename and dirname (they may modify their argument). */
#include "internal.h"
#include <libgen.h>
#include <string.h>

char *basename(char *s)
{
	if (!s || !*s)
		return (char *)".";
	size_t i = strlen(s) - 1;
	for (; i && s[i] == '/'; i--)
		s[i] = 0;
	for (; i && s[i - 1] != '/'; i--) ;
	return s + i;
}

char *dirname(char *s)
{
	if (!s || !*s)
		return (char *)".";
	size_t i = strlen(s) - 1;
	for (; s[i] == '/'; i--)
		if (!i)
			return (char *)"/";
	for (; s[i] != '/'; i--)
		if (!i)
			return (char *)".";
	for (; s[i] == '/'; i--)
		if (!i)
			return (char *)"/";
	s[i + 1] = 0;
	return s;
}
