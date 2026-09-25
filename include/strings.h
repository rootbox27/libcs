#ifndef _STRINGS_H
#define _STRINGS_H
#include <features.h>
#define __need_size_t
#include <stddef.h>
__BEGIN_DECLS
int bcmp(const void *, const void *, size_t);
void bcopy(const void *, void *, size_t);
void bzero(void *, size_t);
int ffs(int);
int ffsl(long);
int ffsll(long long);
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
char *index(const char *, int);
char *rindex(const char *, int);
__END_DECLS
#endif
