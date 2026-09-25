/* Shared type definitions for x86_64 Linux (LP64). */
#ifndef _BITS_ALLTYPES_H
#define _BITS_ALLTYPES_H
#define __need_size_t
#define __need_NULL
#define __need_wchar_t
#include <stddef.h>
typedef long ssize_t;
typedef long off_t;
typedef long off64_t;
typedef int pid_t;
typedef unsigned uid_t;
typedef unsigned gid_t;
typedef unsigned mode_t;
typedef unsigned long nlink_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef long blksize_t;
typedef long blkcnt_t;
typedef long time_t;
typedef long suseconds_t;
typedef int clockid_t;
typedef long clock_t;
typedef unsigned useconds_t;
typedef int id_t;
typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
typedef int key_t;
typedef void *timer_t;
typedef unsigned wint_t;
typedef unsigned long pthread_t;
typedef struct { size_t __stacksize, __guardsize; int __detach; void *__stackaddr; } pthread_attr_t;
#ifndef __DEFINED_timespec
#define __DEFINED_timespec
struct timespec { time_t tv_sec; long tv_nsec; };
#endif
#ifndef __DEFINED_timeval
#define __DEFINED_timeval
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
#endif
#ifndef __DEFINED_sigset
#define __DEFINED_sigset
typedef struct { unsigned long __bits[128/sizeof(long)]; } sigset_t;
#endif
struct iovec { void *iov_base; size_t iov_len; };
#endif
