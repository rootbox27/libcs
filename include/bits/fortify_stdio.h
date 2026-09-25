#ifndef _BITS_FORTIFY_STDIO_H
#define _BITS_FORTIFY_STDIO_H
__BEGIN_DECLS
int __vsnprintf_chk(char *, size_t, int, size_t, const char *, va_list);
int __vsprintf_chk(char *, int, size_t, const char *, va_list);
char *__fgets_chk(char *, size_t, int, FILE *);
size_t __fread_chk(void *, size_t, size_t, size_t, FILE *);

__fortify_inline int vsnprintf(char *__restrict d, size_t n, const char *__restrict f, va_list ap)
{ return __vsnprintf_chk(d, n, 1, __bos(d), f, ap); }
__fortify_inline int vsprintf(char *__restrict d, const char *__restrict f, va_list ap)
{ return __vsprintf_chk(d, 1, __bos(d), f, ap); }
__fortify_inline int snprintf(char *__restrict d, size_t n, const char *__restrict f, ...)
{ return __builtin___snprintf_chk(d, n, 1, __bos(d), f, __builtin_va_arg_pack()); }
__fortify_inline int sprintf(char *__restrict d, const char *__restrict f, ...)
{ return __builtin___sprintf_chk(d, 1, __bos(d), f, __builtin_va_arg_pack()); }
__fortify_inline char *fgets(char *__restrict s, int n, FILE *__restrict f)
{ return __fgets_chk(s, __bos(s), n, f); }
__fortify_inline size_t fread(void *__restrict p, size_t sz, size_t n, FILE *__restrict f)
{ return __fread_chk(p, __bos0(p), sz, n, f); }
__END_DECLS
#endif
