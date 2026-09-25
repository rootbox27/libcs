#ifndef _ERR_H
#define _ERR_H
#include <features.h>
#include <stdarg.h>
__BEGIN_DECLS
__noreturn void err(int, const char *, ...) __printflike(2, 3);
__noreturn void errx(int, const char *, ...) __printflike(2, 3);
__noreturn void verr(int, const char *, va_list) __printflike(2, 0);
__noreturn void verrx(int, const char *, va_list) __printflike(2, 0);
void warn(const char *, ...) __printflike(1, 2);
void warnx(const char *, ...) __printflike(1, 2);
void vwarn(const char *, va_list) __printflike(1, 0);
void vwarnx(const char *, va_list) __printflike(1, 0);
__END_DECLS
#endif
