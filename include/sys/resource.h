#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
typedef unsigned long rlim_t;
struct rlimit { rlim_t rlim_cur, rlim_max; };
struct rusage {
	struct timeval ru_utime, ru_stime;
	long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap,
	     ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
	long __reserved[16];
};
#define RLIM_INFINITY (~0UL)
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NPROC 6
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD 1
#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
int prlimit(pid_t, int, const struct rlimit *, struct rlimit *);
int getrusage(int, struct rusage *);
int getpriority(int, id_t);
int setpriority(int, id_t, int);
__END_DECLS
#endif
