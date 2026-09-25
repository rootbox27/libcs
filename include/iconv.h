#ifndef _ICONV_H
#define _ICONV_H
#include <features.h>
#include <stddef.h>
__BEGIN_DECLS
typedef void *iconv_t;
iconv_t iconv_open(const char *, const char *);
size_t iconv(iconv_t, char **__restrict, size_t *__restrict, char **__restrict, size_t *__restrict);
int iconv_close(iconv_t);
__END_DECLS
#endif
