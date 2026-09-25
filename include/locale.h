#ifndef _LOCALE_H
#define _LOCALE_H
#include <features.h>
#define __need_NULL
#include <stddef.h>
__BEGIN_DECLS
#define LC_CTYPE 0
#define LC_NUMERIC 1
#define LC_TIME 2
#define LC_COLLATE 3
#define LC_MONETARY 4
#define LC_MESSAGES 5
#define LC_ALL 6
struct lconv {
	char *decimal_point, *thousands_sep, *grouping, *int_curr_symbol, *currency_symbol,
	     *mon_decimal_point, *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
	char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space, n_cs_precedes,
	     n_sep_by_space, p_sign_posn, n_sign_posn, int_p_cs_precedes, int_p_sep_by_space,
	     int_n_cs_precedes, int_n_sep_by_space, int_p_sign_posn, int_n_sign_posn;
};
typedef struct __citadel_locale *locale_t;
#define LC_GLOBAL_LOCALE ((locale_t)-1)
char *setlocale(int, const char *);
struct lconv *localeconv(void);
locale_t uselocale(locale_t);
locale_t newlocale(int, const char *, locale_t);
void freelocale(locale_t);
__END_DECLS
#endif
