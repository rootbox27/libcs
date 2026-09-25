#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H
#include <bits/alltypes.h>
struct tms { clock_t tms_utime, tms_stime, tms_cutime, tms_cstime; };
clock_t times(struct tms *);
#endif
