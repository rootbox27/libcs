#ifndef _TIME_H
#define _TIME_H
#include <features.h>
#include <bits/alltypes.h>
#define __need_NULL
#include <stddef.h>
__BEGIN_DECLS
#define CLOCKS_PER_SEC 1000000L
#define TIME_UTC 1
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define TIMER_ABSTIME 1
struct tm {
	int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
	long tm_gmtoff;
	const char *tm_zone;
};
struct itimerspec { struct timespec it_interval, it_value; };
struct sigevent;
time_t time(time_t *);
clock_t clock(void);
double difftime(time_t, time_t);
time_t mktime(struct tm *);
time_t timegm(struct tm *);
struct tm *gmtime(const time_t *);
struct tm *gmtime_r(const time_t *__restrict, struct tm *__restrict);
struct tm *localtime(const time_t *);
struct tm *localtime_r(const time_t *__restrict, struct tm *__restrict);
char *asctime(const struct tm *);
char *asctime_r(const struct tm *__restrict, char *__restrict);
char *ctime(const time_t *);
char *ctime_r(const time_t *, char *);
size_t strftime(char *__restrict, size_t, const char *__restrict, const struct tm *__restrict);
char *strptime(const char *__restrict, const char *__restrict, struct tm *__restrict);
int timespec_get(struct timespec *, int);
int clock_gettime(clockid_t, struct timespec *);
int clock_settime(clockid_t, const struct timespec *);
int clock_getres(clockid_t, struct timespec *);
int clock_nanosleep(clockid_t, int, const struct timespec *, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
void tzset(void);
extern char *tzname[2];
extern long timezone;
extern int daylight;
struct sigevent;
int timer_create(clockid_t, struct sigevent *__restrict, timer_t *__restrict);
int timer_delete(timer_t);
int timer_settime(timer_t, int, const struct itimerspec *__restrict, struct itimerspec *__restrict);
int timer_gettime(timer_t, struct itimerspec *);
int timer_getoverrun(timer_t);
__END_DECLS
#endif
