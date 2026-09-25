#ifndef _ALLOCA_H
#define _ALLOCA_H
#define __need_size_t
#include <stddef.h>
void *alloca(size_t);
#define alloca __builtin_alloca
#endif
