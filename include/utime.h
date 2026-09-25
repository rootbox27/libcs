#ifndef _UTIME_H
#define _UTIME_H
#include <bits/alltypes.h>
struct utimbuf { time_t actime, modtime; };
int utime(const char *, const struct utimbuf *);
#endif
