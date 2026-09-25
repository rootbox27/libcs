#ifndef _EXECINFO_H
#define _EXECINFO_H
#include <features.h>
__BEGIN_DECLS
int backtrace(void **, int);
char **backtrace_symbols(void *const *, int);
void backtrace_symbols_fd(void *const *, int, int);
__END_DECLS
#endif
