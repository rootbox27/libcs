/* _FORTIFY_SOURCE bounds checks for string functions.
 * Each wrapper is an always-inline gnu_inline function, so the object size
 * of the destination is known at the call site; the checked __*_chk entry
 * points abort the process on overflow. */
#ifndef _BITS_FORTIFY_STRING_H
#define _BITS_FORTIFY_STRING_H
__BEGIN_DECLS
void *__memcpy_chk(void *, const void *, size_t, size_t);
void *__memmove_chk(void *, const void *, size_t, size_t);
void *__memset_chk(void *, int, size_t, size_t);
char *__strcpy_chk(char *, const char *, size_t);
char *__stpcpy_chk(char *, const char *, size_t);
char *__strncpy_chk(char *, const char *, size_t, size_t);
char *__strcat_chk(char *, const char *, size_t);
char *__strncat_chk(char *, const char *, size_t, size_t);
size_t __strlcpy_chk(char *, const char *, size_t, size_t);
size_t __strlcat_chk(char *, const char *, size_t, size_t);

__fortify_inline void *memcpy(void *__restrict d, const void *__restrict s, size_t n)
{ return __builtin___memcpy_chk(d, s, n, __bos0(d)); }
__fortify_inline void *memmove(void *d, const void *s, size_t n)
{ return __builtin___memmove_chk(d, s, n, __bos0(d)); }
__fortify_inline void *memset(void *d, int c, size_t n)
{ return __builtin___memset_chk(d, c, n, __bos0(d)); }
__fortify_inline char *strcpy(char *__restrict d, const char *__restrict s)
{ return __builtin___strcpy_chk(d, s, __bos(d)); }
__fortify_inline char *stpcpy(char *__restrict d, const char *__restrict s)
{ return __builtin___stpcpy_chk(d, s, __bos(d)); }
__fortify_inline char *strncpy(char *__restrict d, const char *__restrict s, size_t n)
{ return __builtin___strncpy_chk(d, s, n, __bos(d)); }
__fortify_inline char *strcat(char *__restrict d, const char *__restrict s)
{ return __builtin___strcat_chk(d, s, __bos(d)); }
__fortify_inline char *strncat(char *__restrict d, const char *__restrict s, size_t n)
{ return __builtin___strncat_chk(d, s, n, __bos(d)); }
__fortify_inline size_t strlcpy(char *__restrict d, const char *__restrict s, size_t n)
{ return __strlcpy_chk(d, s, n, __bos(d)); }
__fortify_inline size_t strlcat(char *__restrict d, const char *__restrict s, size_t n)
{ return __strlcat_chk(d, s, n, __bos(d)); }
__END_DECLS
#endif
