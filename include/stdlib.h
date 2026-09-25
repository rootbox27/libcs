#ifndef _STDLIB_H
#define _STDLIB_H
#include <features.h>
#define __need_size_t
#define __need_wchar_t
#define __need_NULL
#include <stddef.h>
__BEGIN_DECLS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
#define MB_CUR_MAX ((size_t)4)
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

void *malloc(size_t) __attribute__((__malloc__, __alloc_size__(1))) __wur;
void *calloc(size_t, size_t) __attribute__((__malloc__, __alloc_size__(1,2))) __wur;
void *realloc(void *, size_t) __attribute__((__alloc_size__(2))) __wur;
void *reallocarray(void *, size_t, size_t) __attribute__((__alloc_size__(2,3))) __wur;
void *recallocarray(void *, size_t, size_t, size_t) __wur;
void free(void *);
void freezero(void *, size_t);
void *aligned_alloc(size_t, size_t) __attribute__((__malloc__, __alloc_align__(1), __alloc_size__(2))) __wur;
int posix_memalign(void **, size_t, size_t) __wur;
void *valloc(size_t) __wur;

int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
double atof(const char *);
long strtol(const char *__restrict, char **__restrict, int);
unsigned long strtoul(const char *__restrict, char **__restrict, int);
long long strtoll(const char *__restrict, char **__restrict, int);
unsigned long long strtoull(const char *__restrict, char **__restrict, int);
long long strtonum(const char *, long long, long long, const char **);
double strtod(const char *__restrict, char **__restrict);
float strtof(const char *__restrict, char **__restrict);
long double strtold(const char *__restrict, char **__restrict);

int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);

void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void qsort_r(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));

int rand(void);
void srand(unsigned);
int rand_r(unsigned *);
long random(void);
void srandom(unsigned);
unsigned arc4random(void);
void arc4random_buf(void *, size_t);
unsigned arc4random_uniform(unsigned);

__noreturn void abort(void);
__noreturn void exit(int);
__noreturn void _Exit(int);
__noreturn void quick_exit(int);
int atexit(void (*)(void));
int at_quick_exit(void (*)(void));

char *getenv(const char *);
char *secure_getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *);
int putenv(char *);
int clearenv(void);
int system(const char *);
char *realpath(const char *__restrict, char *__restrict);
int mkstemp(char *);
int mkostemp(char *, int);
int mkstemps(char *, int);
int mkostemps(char *, int, int);
char *mkdtemp(char *);
int getloadavg(double *, int);

int mblen(const char *, size_t);
int mbtowc(wchar_t *__restrict, const char *__restrict, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *__restrict, const char *__restrict, size_t);
size_t wcstombs(char *__restrict, const wchar_t *__restrict, size_t);

const char *getprogname(void);
void setprogname(const char *);
int posix_openpt(int);
int grantpt(int);
int unlockpt(int);
char *ptsname(int);
int ptsname_r(int, char *, size_t);
__END_DECLS
#include <alloca.h>
#endif
