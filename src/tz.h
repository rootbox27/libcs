/* Internal calendar and time zone helpers. */
#ifndef CITADEL_TZ_H
#define CITADEL_TZ_H
#include "internal.h"

hidden int __is_leap(long long y);
hidden long long __days_from_civil(long long y, unsigned m, unsigned d);
hidden void __tz_lookup(long long t, long *off, int *isdst, const char **name);

#endif
