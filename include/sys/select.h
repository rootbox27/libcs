#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
#define FD_SETSIZE 1024
typedef unsigned long fd_mask;
typedef struct { unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)]; } fd_set;
/* FD_SET/FD_CLR/FD_ISSET abort on out-of-range descriptors instead of
 * silently corrupting adjacent memory. */
void __fd_check(int);
#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof(fd_set)/sizeof(long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s) (__fd_check(d), (s)->fds_bits[(d) / (8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s) (__fd_check(d), (s)->fds_bits[(d) / (8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) (__fd_check(d), !!((s)->fds_bits[(d) / (8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long))))))
int select(int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, struct timeval *__restrict);
int pselect(int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict, const struct timespec *__restrict, const sigset_t *__restrict);
__END_DECLS
#endif
