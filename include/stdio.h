#ifndef _STDIO_H
#define _STDIO_H
#include <features.h>
#define __need_size_t
#define __need_NULL
#include <stddef.h>
#include <stdarg.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
typedef struct __citadel_file FILE;
typedef struct { long long __pos; } fpos_t;
#define EOF (-1)
#define BUFSIZ 8192
#define FILENAME_MAX 4096
#define FOPEN_MAX 1000
#define L_tmpnam 20
#define TMP_MAX 10000
#define P_tmpdir "/tmp"
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
#define stdin (stdin)
#define stdout (stdout)
#define stderr (stderr)

FILE *fopen(const char *__restrict, const char *__restrict) __wur;
FILE *fdopen(int, const char *) __wur;
FILE *freopen(const char *__restrict, const char *__restrict, FILE *__restrict);
FILE *fmemopen(void *__restrict, size_t, const char *__restrict);
FILE *open_memstream(char **, size_t *);
FILE *tmpfile(void);
FILE *popen(const char *, const char *);
int pclose(FILE *);
int fclose(FILE *);
int fflush(FILE *);
int fileno(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fseek(FILE *, long, int);
int fseeko(FILE *, off_t, int);
long ftell(FILE *);
off_t ftello(FILE *);
void rewind(FILE *);
int fgetpos(FILE *__restrict, fpos_t *__restrict);
int fsetpos(FILE *, const fpos_t *);
int setvbuf(FILE *__restrict, char *__restrict, int, size_t);
void setbuf(FILE *__restrict, char *__restrict);
void setlinebuf(FILE *);

size_t fread(void *__restrict, size_t, size_t, FILE *__restrict) __wur;
size_t fwrite(const void *__restrict, size_t, size_t, FILE *__restrict);
int fgetc(FILE *);
int getc(FILE *);
int getchar(void);
int fputc(int, FILE *);
int putc(int, FILE *);
int putchar(int);
int ungetc(int, FILE *);
char *fgets(char *__restrict, int, FILE *__restrict) __wur;
int fputs(const char *__restrict, FILE *__restrict);
int puts(const char *);
ssize_t getline(char **__restrict, size_t *__restrict, FILE *__restrict);
ssize_t getdelim(char **__restrict, size_t *__restrict, int, FILE *__restrict);
int getc_unlocked(FILE *);
int getchar_unlocked(void);
int putc_unlocked(int, FILE *);
int putchar_unlocked(int);
int fgetc_unlocked(FILE *);
int fputc_unlocked(int, FILE *);
void flockfile(FILE *);
void funlockfile(FILE *);
int ftrylockfile(FILE *);

int printf(const char *__restrict, ...) __printflike(1, 2);
int fprintf(FILE *__restrict, const char *__restrict, ...) __printflike(2, 3);
int dprintf(int, const char *__restrict, ...) __printflike(2, 3);
int sprintf(char *__restrict, const char *__restrict, ...) __printflike(2, 3);
int snprintf(char *__restrict, size_t, const char *__restrict, ...) __printflike(3, 4);
int asprintf(char **, const char *, ...) __printflike(2, 3) __wur;
int vprintf(const char *__restrict, va_list) __printflike(1, 0);
int vfprintf(FILE *__restrict, const char *__restrict, va_list) __printflike(2, 0);
int vdprintf(int, const char *__restrict, va_list) __printflike(2, 0);
int vsprintf(char *__restrict, const char *__restrict, va_list) __printflike(2, 0);
int vsnprintf(char *__restrict, size_t, const char *__restrict, va_list) __printflike(3, 0);
int vasprintf(char **, const char *, va_list) __printflike(2, 0) __wur;

int scanf(const char *__restrict, ...) __scanflike(1, 2);
int fscanf(FILE *__restrict, const char *__restrict, ...) __scanflike(2, 3);
int sscanf(const char *__restrict, const char *__restrict, ...) __scanflike(2, 3);
int vscanf(const char *__restrict, va_list) __scanflike(1, 0);
int vfscanf(FILE *__restrict, const char *__restrict, va_list) __scanflike(2, 0);
int vsscanf(const char *__restrict, const char *__restrict, va_list) __scanflike(2, 0);

void perror(const char *);
int remove(const char *);
int rename(const char *, const char *);
int renameat(int, const char *, int, const char *);
char *ctermid(char *);
/* gets() is intentionally not provided: it cannot be used safely. */
__END_DECLS
#ifdef __CITADEL_FORTIFY
#include <bits/fortify_stdio.h>
#endif
#endif
