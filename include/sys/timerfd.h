#ifndef _SYS_TIMERFD_H
#define _SYS_TIMERFD_H
#include <features.h>
#include <time.h>
__BEGIN_DECLS
#define TFD_NONBLOCK 04000
#define TFD_CLOEXEC 02000000
#define TFD_TIMER_ABSTIME 1
#define TFD_TIMER_CANCEL_ON_SET 2
int timerfd_create(int, int);
int timerfd_settime(int, int, const struct itimerspec *, struct itimerspec *);
int timerfd_gettime(int, struct itimerspec *);
__END_DECLS
#endif
