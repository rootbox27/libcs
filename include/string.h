#ifndef _STRING_H
#define _STRING_H
#include <features.h>
#define __need_size_t
#define __need_NULL
#include <stddef.h>
__BEGIN_DECLS
void *memcpy(void *__restrict, const void *__restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
void *memrchr(const void *, int, size_t);
void *memmem(const void *, size_t, const void *, size_t);
void *mempcpy(void *__restrict, const void *__restrict, size_t);
void *memccpy(void *__restrict, const void *__restrict, int, size_t);
char *strcpy(char *__restrict, const char *__restrict);
char *strncpy(char *__restrict, const char *__restrict, size_t);
char *stpcpy(char *__restrict, const char *__restrict);
char *stpncpy(char *__restrict, const char *__restrict, size_t);
char *strcat(char *__restrict, const char *__restrict);
char *strncat(char *__restrict, const char *__restrict, size_t);
size_t strlcpy(char *__restrict, const char *__restrict, size_t);
size_t strlcat(char *__restrict, const char *__restrict, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcoll(const char *, const char *);
size_t strxfrm(char *__restrict, const char *__restrict, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strchrnul(const char *, int);
size_t strcspn(const char *, const char *);
size_t strspn(const char *, const char *);
char *strpbrk(const char *, const char *);
char *strstr(const char *, const char *);
char *strcasestr(const char *, const char *);
char *strtok(char *__restrict, const char *__restrict);
char *strtok_r(char *__restrict, const char *__restrict, char **__restrict);
char *strsep(char **, const char *);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
char *strdup(const char *);
char *strndup(const char *, size_t);
char *strerror(int);
int strerror_r(int, char *, size_t);
const char *strerrorname_np(int);
char *strsignal(int);
void explicit_bzero(void *, size_t);
int timingsafe_bcmp(const void *, const void *, size_t);
int timingsafe_memcmp(const void *, const void *, size_t);
int strverscmp(const char *, const char *);
#include <strings.h>
__END_DECLS
#ifdef __CITADEL_FORTIFY
#include <bits/fortify_string.h>
#endif
#endif
