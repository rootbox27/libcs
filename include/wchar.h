#ifndef _WCHAR_H
#define _WCHAR_H
#include <features.h>
#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#include <stddef.h>
#include <stdarg.h>
#include <bits/alltypes.h>
#include <bits/locale_t.h>
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

int fwide(FILE *, int);
wint_t fgetwc(FILE *);
wint_t getwc(FILE *);
wint_t getwchar(void);
wchar_t *fgetws(wchar_t *__restrict, int, FILE *__restrict);
wint_t fputwc(wchar_t, FILE *);
wint_t putwc(wchar_t, FILE *);
wint_t putwchar(wchar_t);
int fputws(const wchar_t *__restrict, FILE *__restrict);
wint_t ungetwc(wint_t, FILE *);
wint_t fgetwc_unlocked(FILE *);
wint_t getwc_unlocked(FILE *);
wint_t getwchar_unlocked(void);
wchar_t *fgetws_unlocked(wchar_t *__restrict, int, FILE *__restrict);
wint_t fputwc_unlocked(wchar_t, FILE *);
wint_t putwc_unlocked(wchar_t, FILE *);
wint_t putwchar_unlocked(wchar_t);
int fputws_unlocked(const wchar_t *__restrict, FILE *__restrict);

int fwprintf(FILE *__restrict, const wchar_t *__restrict, ...);
int wprintf(const wchar_t *__restrict, ...);
int swprintf(wchar_t *__restrict, size_t, const wchar_t *__restrict, ...);
int vfwprintf(FILE *__restrict, const wchar_t *__restrict, va_list);
int vwprintf(const wchar_t *__restrict, va_list);
int vswprintf(wchar_t *__restrict, size_t, const wchar_t *__restrict, va_list);
int fwscanf(FILE *__restrict, const wchar_t *__restrict, ...);
int wscanf(const wchar_t *__restrict, ...);
int swscanf(const wchar_t *__restrict, const wchar_t *__restrict, ...);
int vfwscanf(FILE *__restrict, const wchar_t *__restrict, va_list);
int vwscanf(const wchar_t *__restrict, va_list);
int vswscanf(const wchar_t *__restrict, const wchar_t *__restrict, va_list);
int wcscoll_l(const wchar_t *, const wchar_t *, locale_t);
size_t wcsxfrm_l(wchar_t *__restrict, const wchar_t *__restrict, size_t, locale_t);
size_t mbsnrtowcs(wchar_t *__restrict, const char **__restrict, size_t, size_t, mbstate_t *__restrict);
size_t wcsnrtombs(char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict);
int wcscasecmp(const wchar_t *, const wchar_t *);
int wcsncasecmp(const wchar_t *, const wchar_t *, size_t);
int wcscasecmp_l(const wchar_t *, const wchar_t *, locale_t);
int wcsncasecmp_l(const wchar_t *, const wchar_t *, size_t, locale_t);
__END_DECLS
#endif
