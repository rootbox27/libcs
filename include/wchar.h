#ifndef _WCHAR_H
#define _WCHAR_H
#include <features.h>
#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#include <stddef.h>
#include <stdarg.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
typedef struct { unsigned __st, __n; } mbstate_t;
#define WEOF 0xffffffffU
#ifndef WCHAR_MIN
#define WCHAR_MIN (-1-0x7fffffff)
#define WCHAR_MAX 0x7fffffff
#endif
struct tm;
typedef struct __citadel_file FILE;
size_t wcslen(const wchar_t *);
size_t wcsnlen(const wchar_t *, size_t);
wchar_t *wcscpy(wchar_t *__restrict, const wchar_t *__restrict);
wchar_t *wcsncpy(wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wcscat(wchar_t *__restrict, const wchar_t *__restrict);
int wcscmp(const wchar_t *, const wchar_t *);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wcschr(const wchar_t *, wchar_t);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
wchar_t *wcsdup(const wchar_t *);
wchar_t *wmemcpy(wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);
wchar_t *wmemset(wchar_t *, wchar_t, size_t);
int wmemcmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);
long wcstol(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long wcstoul(const wchar_t *__restrict, wchar_t **__restrict, int);
long long wcstoll(const wchar_t *__restrict, wchar_t **__restrict, int);
unsigned long long wcstoull(const wchar_t *__restrict, wchar_t **__restrict, int);
float wcstof(const wchar_t *__restrict, wchar_t **__restrict);
double wcstod(const wchar_t *__restrict, wchar_t **__restrict);
long double wcstold(const wchar_t *__restrict, wchar_t **__restrict);
size_t wcsspn(const wchar_t *, const wchar_t *);
size_t wcscspn(const wchar_t *, const wchar_t *);
wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
wchar_t *wcsncat(wchar_t *__restrict, const wchar_t *__restrict, size_t);
wchar_t *wcstok(wchar_t *__restrict, const wchar_t *__restrict, wchar_t **__restrict);
wchar_t *wcsstr(const wchar_t *__restrict, const wchar_t *__restrict);
int wcscoll(const wchar_t *, const wchar_t *);
size_t wcsxfrm(wchar_t *__restrict, const wchar_t *__restrict, size_t);
size_t wcsftime(wchar_t *__restrict, size_t, const wchar_t *__restrict, const struct tm *__restrict);
int wcwidth(wchar_t);
int wcswidth(const wchar_t *, size_t);
wint_t btowc(int);
int wctob(wint_t);
int mbsinit(const mbstate_t *);
size_t mbrtowc(wchar_t *__restrict, const char *__restrict, size_t, mbstate_t *__restrict);
size_t wcrtomb(char *__restrict, wchar_t, mbstate_t *__restrict);
size_t mbrlen(const char *__restrict, size_t, mbstate_t *__restrict);
size_t mbsrtowcs(wchar_t *__restrict, const char **__restrict, size_t, mbstate_t *__restrict);
size_t wcsrtombs(char *__restrict, const wchar_t **__restrict, size_t, mbstate_t *__restrict);
__END_DECLS
#endif
