#ifndef _MALLOC_H
#define _MALLOC_H
#include <stdlib.h>
__BEGIN_DECLS
size_t malloc_usable_size(void *);
void *memalign(size_t, size_t);
void *pvalloc(size_t);
int malloc_trim(size_t);
__END_DECLS
#endif
