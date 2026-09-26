/* Locales. The only locale is C (also reachable as "POSIX", "C.UTF-8"
 * and the empty string); its character encoding is UTF-8. */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

struct __citadel_locale {
	int dummy;
};

static struct __citadel_locale c_locale;

static int known(const char *name)
{
	return !*name || !strcmp(name, "C") || !strcmp(name, "POSIX") || !strcmp(name, "C.UTF-8") ||
	       !strcmp(name, "C.utf8");
}

char *setlocale(int cat, const char *name)
{
	if ((unsigned)cat > LC_ALL)
		return 0;
	if (name && !known(name))
		return 0;
	return (char *)"C";
}

struct lconv *localeconv(void)
{
	static struct lconv lc = {
		".", "", "", "", "", "", "", "", "", "",
		CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
		CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
	};
	return &lc;
}

static __thread locale_t current;

locale_t uselocale(locale_t l)
{
	locale_t old = current ? current : LC_GLOBAL_LOCALE;
	if (l)
		current = l == LC_GLOBAL_LOCALE ? 0 : l;
	return old;
}

locale_t newlocale(int mask, const char *name, locale_t base)
{
	if ((mask & ~LC_ALL_MASK) || !name) {
		errno = EINVAL;
		return 0;
	}
	if (!known(name)) {
		errno = ENOENT;
		return 0;
	}
	return &c_locale;
}

void freelocale(locale_t l)
{
}

/* every locale object is the one C locale */
locale_t duplocale(locale_t l)
{
	return &c_locale;
}
