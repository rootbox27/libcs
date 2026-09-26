/* Locales. The only locale data is C's, and the character encoding is
 * always UTF-8. Any name with a UTF-8 codeset ("en_US.UTF-8",
 * "de_DE.utf8@euro") is accepted and behaves as C.UTF-8, so programs that
 * ask for the user's UTF-8 locale work; "C" and "POSIX" are the C locale.
 * Names with another codeset, or none (which means a legacy 8-bit one),
 * are refused, as glibc refuses a locale that is not installed. The names
 * are remembered per category so queries report what was set. */
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct __citadel_locale {
	int dummy;
};

static struct __citadel_locale c_locale;

#define NAME_MAX_LEN 64

static int known(const char *name)
{
	if (!strcmp(name, "C") || !strcmp(name, "POSIX"))
		return 1;
	size_t n = strlen(name);
	if (!n || n >= NAME_MAX_LEN || strchr(name, '/'))
		return 0;
	/* the codeset: after '.', up to an optional '@modifier' */
	const char *dot = strchr(name, '.');
	if (!dot)
		return 0;
	size_t l = strcspn(dot + 1, "@");
	return (l == 5 && !strncasecmp(dot + 1, "utf-8", 5)) || (l == 4 && !strncasecmp(dot + 1, "utf8", 4));
}

static const char *const cat_env[LC_ALL] = {
	"LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
};

/* "" means the environment: LC_ALL, then LC_<category>, then LANG, then C */
static const char *from_env(int cat)
{
	const char *s = getenv("LC_ALL");
	if (!s || !*s)
		s = getenv(cat_env[cat]);
	if (!s || !*s)
		s = getenv("LANG");
	return s && *s ? s : "C";
}

static char names[LC_ALL][NAME_MAX_LEN] = { "C", "C", "C", "C", "C", "C" };

char *setlocale(int cat, const char *name)
{
	static char all[LC_ALL * (NAME_MAX_LEN + 16)];
	if ((unsigned)cat > LC_ALL)
		return 0;
	if (name) {
		/* resolve and check every affected category before changing any */
		const char *want[LC_ALL];
		for (int c = 0; c < LC_ALL; c++) {
			want[c] = 0;
			if (cat != LC_ALL && c != cat)
				continue;
			want[c] = *name ? name : from_env(c);
			if (!known(want[c]))
				return 0;
		}
		for (int c = 0; c < LC_ALL; c++)
			if (want[c])
				strcpy(names[c], !strcmp(want[c], "POSIX") ? "C" : want[c]);
	}
	if (cat != LC_ALL)
		return names[cat];
	/* one name if all categories agree, else glibc's composite form */
	int same = 1;
	for (int c = 1; c < LC_ALL; c++)
		if (strcmp(names[c], names[0]))
			same = 0;
	if (same)
		return names[0];
	static const char *const cat_name[LC_ALL] = {
		"LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
	};
	char *p = all;
	for (int c = 0; c < LC_ALL; c++) {
		size_t k = strlen(cat_name[c]), l = strlen(names[c]);
		if (c)
			*p++ = ';';
		memcpy(p, cat_name[c], k);
		p += k;
		*p++ = '=';
		memcpy(p, names[c], l);
		p += l;
	}
	*p = 0;
	return all;
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
	for (int c = 0; c < LC_ALL; c++) {
		if (!(mask & (1 << c)))
			continue;
		if (!known(*name ? name : from_env(c))) {
			errno = ENOENT;
			return 0;
		}
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
