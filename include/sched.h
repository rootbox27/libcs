#ifndef _SCHED_H
#define _SCHED_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
struct sched_param { int sched_priority; };
#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2
#define SCHED_BATCH 3
#define SCHED_IDLE 5
#define CLONE_VM 0x00000100
#define CLONE_FS 0x00000200
#define CLONE_FILES 0x00000400
#define CLONE_SIGHAND 0x00000800
#define CLONE_PIDFD 0x00001000
#define CLONE_VFORK 0x00004000
#define CLONE_PARENT 0x00008000
#define CLONE_THREAD 0x00010000
#define CLONE_NEWNS 0x00020000
#define CLONE_SYSVSEM 0x00040000
#define CLONE_SETTLS 0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID 0x01000000
#define CLONE_NEWUSER 0x10000000
#define CLONE_NEWPID 0x20000000
#define CLONE_NEWNET 0x40000000
typedef struct { unsigned long __bits[128/sizeof(long)]; } cpu_set_t;
#define CPU_SETSIZE 1024
#define CPU_ZERO(s) __builtin_memset((s), 0, sizeof(cpu_set_t))
#define CPU_SET(i, s) ((i) < CPU_SETSIZE ? ((s)->__bits[(i)/64] |= 1UL<<((i)%64)) : 0)
#define CPU_CLR(i, s) ((i) < CPU_SETSIZE ? ((s)->__bits[(i)/64] &= ~(1UL<<((i)%64))) : 0)
#define CPU_ISSET(i, s) ((i) < CPU_SETSIZE ? !!((s)->__bits[(i)/64] & (1UL<<((i)%64))) : 0)
#define CPU_COUNT(s) __sched_cpucount(sizeof(cpu_set_t), (s))
int __sched_cpucount(size_t, const cpu_set_t *);
int sched_yield(void);
int sched_getcpu(void);
int sched_getaffinity(pid_t, size_t, cpu_set_t *);
int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
int sched_get_priority_max(int);
int sched_get_priority_min(int);
int sched_getscheduler(pid_t);
int sched_setscheduler(pid_t, int, const struct sched_param *);
int sched_getparam(pid_t, struct sched_param *);
int sched_setparam(pid_t, const struct sched_param *);
int unshare(int);
__END_DECLS
#endif
