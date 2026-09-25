#ifndef _ULIMIT_H
#define _ULIMIT_H
#include <features.h>
__BEGIN_DECLS
#define UL_GETFSIZE 1
#define UL_SETFSIZE 2
long ulimit(int, ...);
__END_DECLS
#endif
