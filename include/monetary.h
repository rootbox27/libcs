#ifndef _MONETARY_H
#define _MONETARY_H
#include <features.h>
#include <bits/alltypes.h>
#include <locale.h>
__BEGIN_DECLS
ssize_t strfmon(char *__restrict, size_t, const char *__restrict, ...);
ssize_t strfmon_l(char *__restrict, size_t, locale_t, const char *__restrict, ...);
__END_DECLS
#endif
