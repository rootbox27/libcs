#include "harness.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static char buf[256];

static const char *fmt(const char *f, const struct tm *tm)
{
	strftime(buf, sizeof buf, f, tm);
	return buf;
}

static void utc(void)
{
	struct tm tm;
	time_t t = 0;
	CHECK(gmtime_r(&t, &tm) && tm.tm_year == 70 && tm.tm_mon == 0 && tm.tm_mday == 1 && tm.tm_wday == 4 && tm.tm_yday == 0);
	t = 951782400; /* 2000-02-29 */
	CHECK(gmtime_r(&t, &tm) && tm.tm_mon == 1 && tm.tm_mday == 29 && tm.tm_wday == 2 && tm.tm_yday == 59);
	t = -1;
	CHECK(gmtime_r(&t, &tm) && tm.tm_year == 69 && tm.tm_mon == 11 && tm.tm_mday == 31 && tm.tm_hour == 23 && tm.tm_sec == 59);
	t = 253402300799; /* 9999-12-31 23:59:59 */
	CHECK(gmtime_r(&t, &tm) && tm.tm_year == 8099 && tm.tm_yday == 364);
	t = -62135596800; /* 0001-01-01 */
	CHECK(gmtime_r(&t, &tm) && tm.tm_year == -1899 && tm.tm_mon == 0 && tm.tm_mday == 1 && tm.tm_wday == 1);
	t = LONG_MAX;
	errno = 0;
	CHECK(gmtime_r(&t, &tm) == 0 && errno == EOVERFLOW);

	/* timegm normalises and round-trips */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 120;
	tm.tm_mon = 13;      /* Feb 2021 */
	tm.tm_mday = 0;      /* -> Jan 31 */
	tm.tm_hour = 25;
	tm.tm_min = -30;
	CHECK(timegm(&tm) == 1612139400 && tm.tm_year == 121 && tm.tm_mon == 1 && tm.tm_mday == 1 && tm.tm_hour == 0 && tm.tm_min == 30);
	for (time_t x = -5000000000LL; x < 5000000000LL; x += 86399 * 1234 + 17) {
		struct tm g;
		gmtime_r(&x, &g);
		CHECK(timegm(&g) == x);
	}

	t = 1234567890;
	gmtime_r(&t, &tm);
	CHECK(!strcmp(fmt("%Y-%m-%d %H:%M:%S %a %A %b %B %j %u %w %Z %z", &tm),
	              "2009-02-13 23:31:30 Fri Friday Feb February 044 5 5 GMT +0000"));
	CHECK(!strcmp(fmt("%D %T %R %F %e %I %p %y %C %%", &tm), "02/13/09 23:31:30 23:31 2009-02-13 13 11 PM 09 20 %"));
	CHECK(!strcmp(fmt("%U %W %V %G %g", &tm), "06 06 07 2009 09"));
	CHECK(!strcmp(fmt("%-d|%_m|%5Y|%^a|%k|%l", &tm), "13| 2|02009|FRI|23|11"));
	CHECK(!strcmp(fmt("%c", &tm), "Fri Feb 13 23:31:30 2009"));
	/* ISO week edge cases: 2008-12-29 is week 1 of 2009, 2010-01-03 week 53 of 2009 */
	t = 1230508800;
	gmtime_r(&t, &tm);
	CHECK(!strcmp(fmt("%G-W%V-%u", &tm), "2009-W01-1"));
	t = 1262476800;
	gmtime_r(&t, &tm);
	CHECK(!strcmp(fmt("%G-W%V-%u", &tm), "2009-W53-7"));
	CHECK(strftime(buf, 5, "%Y-%m-%d", &tm) == 0);
	CHECK(strftime(buf, sizeof buf, "", &tm) == 0 && buf[0] == 0);

	CHECK(asctime_r(&tm, buf) && !strcmp(buf, "Sun Jan  3 00:00:00 2010\n"));
	tm.tm_year = 200000;
	errno = 0;
	CHECK(asctime_r(&tm, buf) == 0 && errno == EOVERFLOW);

	/* strptime */
	memset(&tm, 0, sizeof tm);
	const char *e = strptime("2024-02-29 13:45:07", "%Y-%m-%d %H:%M:%S", &tm);
	CHECK(e && !*e && tm.tm_year == 124 && tm.tm_mon == 1 && tm.tm_mday == 29 && tm.tm_hour == 13 && tm.tm_min == 45 && tm.tm_sec == 7);
	memset(&tm, 0, sizeof tm);
	e = strptime("Tue, 05 Mar 1996 08:05:00 PM +0130 rest", "%a, %d %b %Y %I:%M:%S %p %z", &tm);
	CHECK(e && !strcmp(e, " rest") && tm.tm_wday == 2 && tm.tm_mon == 2 && tm.tm_hour == 20 && tm.tm_gmtoff == 5400);
	CHECK(strptime("68", "%y", &tm) && tm.tm_year == 168);
	CHECK(strptime("69", "%y", &tm) && tm.tm_year == 69);
	CHECK(strptime("13:61", "%H:%M", &tm) == 0);
	CHECK(strptime("x", "%d", &tm) == 0);
}

static void zones(void)
{
	struct tm tm;
	time_t t = 1593619200; /* 2020-07-01 16:00 UTC */
	setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
	tzset();
	CHECK(!strcmp(tzname[0], "EST") && !strcmp(tzname[1], "EDT") && timezone == 18000 && daylight == 1);
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 12 && tm.tm_isdst == 1 && tm.tm_gmtoff == -14400 && !strcmp(tm.tm_zone, "EDT"));
	t = 1577894400; /* 2020-01-01 16:00 UTC */
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 11 && tm.tm_isdst == 0 && !strcmp(fmt("%z %Z", &tm), "-0500 EST"));
	/* spring-forward gap: 02:30 does not exist; moves forward */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 120; tm.tm_mon = 2; tm.tm_mday = 8; tm.tm_hour = 2; tm.tm_min = 30; tm.tm_isdst = -1;
	CHECK(mktime(&tm) == 1583652600 && tm.tm_hour == 3 && tm.tm_isdst == 1);
	/* fall-back overlap: tm_isdst picks the reading */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 120; tm.tm_mon = 10; tm.tm_mday = 1; tm.tm_hour = 1; tm.tm_min = 30; tm.tm_isdst = 1;
	CHECK(mktime(&tm) == 1604208600 && tm.tm_isdst == 1);
	tm.tm_hour = 1; tm.tm_min = 30; tm.tm_isdst = 0;
	CHECK(mktime(&tm) == 1604212200 && tm.tm_isdst == 0);

	/* southern hemisphere, half-hour offsets, quoted names */
	setenv("TZ", "ACST-9:30ACDT,M10.1.0,M4.1.0/3", 1);
	t = 1577836800; /* 2020-01-01 00:00 UTC: summer */
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 10 && tm.tm_min == 30 && tm.tm_isdst == 1);
	setenv("TZ", "<+0545>-5:45", 1);
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 5 && tm.tm_min == 45 && !strcmp(tm.tm_zone, "+0545"));
	setenv("TZ", "", 1);
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 0 && !strcmp(tm.tm_zone, "UTC"));
	setenv("TZ", "Not/A/Zone", 1);
	CHECK(localtime_r(&t, &tm) && tm.tm_hour == 0 && tm.tm_gmtoff == 0);
	setenv("TZ", "../../../../etc/passwd", 1);
	CHECK(localtime_r(&t, &tm) && tm.tm_gmtoff == 0);

	/* zone files, when the system has them */
	if (access("/usr/share/zoneinfo/Europe/Berlin", R_OK) == 0) {
		setenv("TZ", "Europe/Berlin", 1);
		t = 1593619200;
		CHECK(localtime_r(&t, &tm) && tm.tm_hour == 18 && tm.tm_isdst == 1 && !strcmp(tm.tm_zone, "CEST"));
		t = -2000000000; /* 1906: before modern rules */
		CHECK(localtime_r(&t, &tm) && tm.tm_gmtoff == 3600);
		t = 4102444800LL + 180 * 86400; /* 2100: from the file's POSIX footer */
		CHECK(localtime_r(&t, &tm) && tm.tm_isdst == 1);
		setenv("TZ", ":America/New_York", 1);
		t = 1593619200;
		CHECK(localtime_r(&t, &tm) && tm.tm_hour == 12 && !strcmp(tm.tm_zone, "EDT"));
	}
	unsetenv("TZ");
	tzset();
	char c[26];
	t = 0;
	CHECK(ctime_r(&t, c) != 0);
}

static volatile sig_atomic_t alarmed;
static void on_alarm(int s) { alarmed = 1; }

static void clocks(void)
{
	struct timespec a, b, r;
	CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
	CHECK(clock_gettime(CLOCK_REALTIME, &r) == 0 && r.tv_sec > 1600000000);
	CHECK(time(0) - r.tv_sec <= 1);
	struct timeval tv;
	CHECK(gettimeofday(&tv, 0) == 0 && tv.tv_sec - r.tv_sec <= 1);
	CHECK(timespec_get(&b, TIME_UTC) == TIME_UTC);
	CHECK(clock_getres(CLOCK_MONOTONIC, &b) == 0);
	errno = 0;
	CHECK(clock_gettime(12345, &b) == -1 && errno == EINVAL);
	struct timespec d = { 0, 20000000 };
	CHECK(nanosleep(&d, 0) == 0);
	CHECK(usleep(1000) == 0);
	clock_gettime(CLOCK_MONOTONIC, &b);
	long long ns = (b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec);
	CHECK(ns >= 21000000 && ns < 2000000000);
	d.tv_nsec = 2000000000;
	errno = 0;
	CHECK(nanosleep(&d, 0) == -1 && errno == EINVAL);
	CHECK(clock() >= 0);
	CHECK(difftime(10, 3) == 7.0);

	signal(SIGALRM, on_alarm);
	struct itimerval it = { { 0, 0 }, { 0, 10000 } };
	CHECK(setitimer(ITIMER_REAL, &it, 0) == 0);
	d.tv_sec = 5;
	d.tv_nsec = 0;
	struct timespec rem;
	CHECK(nanosleep(&d, &rem) == -1 && errno == EINTR && alarmed && rem.tv_sec >= 4);
	CHECK(alarm(10) == 0 && alarm(0) >= 9);
}

int main(void)
{
	utc();
	zones();
	clocks();
	return t_done();
}
