#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H
#include <limits.h>
#include <endian.h>
#define MAXPATHLEN PATH_MAX
#define MAXHOSTNAMELEN 64
#define NOFILE 256
#define NGROUPS NGROUPS_MAX
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif
#define howmany(x, y) (((x)+((y)-1))/(y))
#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define powerof2(x) ((((x)-1)&(x))==0)
#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#endif
