/* Broken-down time: gmtime, localtime, timegm, mktime, asctime, ctime,
 * strftime, strptime. Conversions that cannot be represented fail with
 * EOVERFLOW instead of wrapping; asctime never overruns its buffer. */
#include "tz.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const wday_name[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const mon_name[] = { "January", "February", "March", "April", "May", "June", "July",
                                        "August", "September", "October", "November", "December" };

/* ---- conversions ---- */

static struct tm *secs_to_tm(long long t, struct tm *tm)
{
	long long days = t / 86400, rem = t % 86400;
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	/* civil_from_days */
	long long z = days + 719468;
	long long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long long y = (long long)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	unsigned d = doy - (153 * mp + 2) / 5 + 1;
	unsigned m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	if (y - 1900 > INT_MAX || y - 1900 < INT_MIN) {
		errno = EOVERFLOW;
		return 0;
	}
	tm->tm_year = (int)(y - 1900);
	tm->tm_mon = (int)m - 1;
	tm->tm_mday = (int)d;
	tm->tm_yday = (int)(days - __days_from_civil(y, 1, 1));
	tm->tm_wday = (int)(((days % 7) + 11) % 7); /* 1970-01-01: Thursday */
	tm->tm_hour = (int)(rem / 3600);
	tm->tm_min = (int)(rem / 60 % 60);
	tm->tm_sec = (int)(rem % 60);
	return tm;
}

struct tm *gmtime_r(const time_t *__restrict t, struct tm *__restrict tm)
{
	if (!secs_to_tm(*t, tm))
		return 0;
	tm->tm_isdst = 0;
	tm->tm_gmtoff = 0;
	tm->tm_zone = "GMT";
	return tm;
}

struct tm *gmtime(const time_t *t)
{
	static struct tm tm;
	return gmtime_r(t, &tm);
}

struct tm *localtime_r(const time_t *__restrict t, struct tm *__restrict tm)
{
	long off;
	int isdst;
	const char *name;
	/* keep the arithmetic below well away from overflow */
	if (*t < -(1LL << 60) || *t > (1LL << 60)) {
		errno = EOVERFLOW;
		return 0;
	}
	__tz_lookup(*t, &off, &isdst, &name);
	if (!secs_to_tm(*t + off, tm))
		return 0;
	tm->tm_isdst = isdst;
	tm->tm_gmtoff = off;
	tm->tm_zone = name;
	return tm;
}

struct tm *localtime(const time_t *t)
{
	static struct tm tm;
	return localtime_r(t, &tm);
}

/* The broken-down fields as seconds since the epoch, treating them as
 * UTC and normalising out-of-range values. */
static int tm_to_secs(const struct tm *tm, long long *out)
{
	long long y = tm->tm_year + 1900LL, mon = tm->tm_mon;
	y += mon / 12;
	mon %= 12;
	if (mon < 0) {
		mon += 12;
		y--;
	}
	if (y > 1LL << 40 || y < -(1LL << 40))
		return -1;
	long long t = (__days_from_civil(y, (unsigned)mon + 1, 1) + tm->tm_mday - 1) * 86400LL;
	t += tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec;
	*out = t;
	return 0;
}

time_t timegm(struct tm *tm)
{
	long long ll;
	struct tm n;
	time_t t;
	if (tm_to_secs(tm, &ll) || (t = ll, !gmtime_r(&t, &n))) {
		errno = EOVERFLOW;
		return -1;
	}
	*tm = n;
	return t;
}

/* Find an offset of the wanted kind (isdst) in effect near UTC time t,
 * probing outward up to a year. Returns 0 if none exists. */
static int find_kind(long long t, int want, long *off)
{
	for (long long d = 0; d <= 366LL * 86400; d += 15LL * 86400) {
		for (int sgn = -1; sgn <= 1; sgn += 2) {
			long o;
			int dst;
			const char *name;
			__tz_lookup(t + sgn * d, &o, &dst, &name);
			if (dst == want) {
				*off = o;
				return 1;
			}
		}
	}
	return 0;
}

time_t mktime(struct tm *tm)
{
	long long L;
	if (tm_to_secs(tm, &L)) {
		errno = EOVERFLOW;
		return -1;
	}
	/* The local time types in effect within two days of L. */
	struct { long off; int dst; } ty[5];
	int nt = 0;
	for (int k = -2; k <= 2; k++) {
		long o;
		int dst;
		const char *name;
		__tz_lookup(L + k * 86400LL, &o, &dst, &name);
		int seen = 0;
		for (int i = 0; i < nt; i++)
			seen |= ty[i].off == o && ty[i].dst == dst;
		if (!seen) {
			ty[nt].off = o;
			ty[nt].dst = dst;
			nt++;
		}
	}

	long off = ty[0].off;
	if (tm->tm_isdst >= 0) {
		/* Read the time with an offset of the requested kind; if the
		 * zone has none nearby, assume DST is standard time + 1h. */
		int want = tm->tm_isdst > 0;
		int found = 0; /* 1: some type of that kind, 2: a consistent one */
		for (int i = 0; i < nt && found < 2; i++) {
			long o2;
			int d2;
			const char *name;
			if (ty[i].dst != want)
				continue;
			__tz_lookup(L - ty[i].off, &o2, &d2, &name);
			if (o2 == ty[i].off || !found) {
				off = ty[i].off;
				found = o2 == ty[i].off ? 2 : 1;
			}
		}
		if (!found) {
			long o;
			int d0;
			const char *name;
			__tz_lookup(L - ty[0].off, &o, &d0, &name);
			if (!find_kind(L - o, want, &off))
				off = o + (want ? 3600 : -3600) * (d0 == want ? 0 : 1);
		}
	} else {
		/* Prefer a consistent standard-time reading (so an ambiguous
		 * time is taken as the later, standard one); in a gap, use the
		 * smallest offset, which moves the time forward past it. */
		int best = -1;
		for (int i = 0; i < nt; i++) {
			long o;
			int d;
			const char *name;
			__tz_lookup(L - ty[i].off, &o, &d, &name);
			if (o != ty[i].off)
				continue;
			if (best < 0 || (ty[best].dst && !ty[i].dst))
				best = i;
		}
		if (best < 0) {
			best = 0;
			for (int i = 1; i < nt; i++)
				if (ty[i].off < ty[best].off)
					best = i;
		}
		off = ty[best].off;
	}
	time_t tt = L - off;
	struct tm n;
	if (!localtime_r(&tt, &n)) {
		errno = EOVERFLOW;
		return -1;
	}
	*tm = n;
	return tt;
}

/* ---- text forms ---- */

char *asctime_r(const struct tm *__restrict tm, char *__restrict buf)
{
	const char *wd = (unsigned)tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "???";
	const char *mn = (unsigned)tm->tm_mon < 12 ? mon_name[tm->tm_mon] : "???";
	int n = snprintf(buf, 26, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n", wd, mn, tm->tm_mday,
	                 tm->tm_hour, tm->tm_min, tm->tm_sec, 1900 + tm->tm_year);
	if (n < 0 || n >= 26) {
		errno = EOVERFLOW;
		return 0;
	}
	return buf;
}

char *asctime(const struct tm *tm)
{
	static char buf[26];
	return asctime_r(tm, buf);
}

char *ctime_r(const time_t *t, char *buf)
{
	struct tm tm;
	return localtime_r(t, &tm) ? asctime_r(&tm, buf) : 0;
}

char *ctime(const time_t *t)
{
	static char buf[26];
	return ctime_r(t, buf);
}

/* ---- strftime ---- */

/* ISO 8601 week number and year of tm. */
static int iso_week(const struct tm *tm, long long *iso_year)
{
	long long y = tm->tm_year + 1900LL;
	int wday = (tm->tm_wday + 6) % 7; /* Monday = 0 */
	int week = (tm->tm_yday - wday + 10) / 7;
	if (week < 1) {
		y--;
		int prev_len = 365 + __is_leap(y);
		week = (tm->tm_yday + prev_len - wday + 10) / 7;
	} else if (week == 53) {
		int len = 365 + __is_leap(y);
		if (tm->tm_yday - wday + 3 >= len) {
			week = 1;
			y++;
		}
	}
	*iso_year = y;
	return week;
}

struct sbuf {
	char *s;
	size_t n, max;
	int full;
};

static void put(struct sbuf *b, const char *s, size_t l)
{
	if (b->n + l >= b->max) {
		b->full = 1;
		return;
	}
	memcpy(b->s + b->n, s, l);
	b->n += l;
}

static void put_num(struct sbuf *b, long long v, int width, char pad)
{
	char tmp[32];
	int neg = v < 0;
	unsigned long long u = neg ? -(unsigned long long)v : (unsigned long long)v;
	int i = sizeof tmp;
	do tmp[--i] = (char)('0' + u % 10); while (u /= 10);
	int digits = (int)sizeof tmp - i;
	if (pad && width > digits + neg) {
		if (pad == '0') {
			while (width > digits + neg && i > 1) {
				tmp[--i] = '0';
				width--;
			}
		} else {
			if (neg)
				tmp[--i] = '-';
			neg = 0;
			while ((int)sizeof tmp - i < width && i > 0)
				tmp[--i] = ' ';
		}
	}
	if (neg)
		tmp[--i] = '-';
	put(b, tmp + i, sizeof tmp - (size_t)i);
}

static size_t fmt_tm(struct sbuf *b, const char *f, const struct tm *tm);

size_t strftime(char *__restrict s, size_t max, const char *__restrict f, const struct tm *__restrict tm)
{
	struct sbuf b = { s, 0, max, 0 };
	fmt_tm(&b, f, tm);
	if (b.full || !max)
		return 0;
	s[b.n] = 0;
	return b.n;
}

static size_t fmt_tm(struct sbuf *b, const char *f, const struct tm *tm)
{
	for (; *f && !b->full; f++) {
		if (*f != '%') {
			put(b, f, 1);
			continue;
		}
		f++;
		/* GNU flags and width */
		char pad = 0;
		int upper = 0, width = 0;
		for (;; f++) {
			if (*f == '_') pad = ' ';
			else if (*f == '-') pad = '-';
			else if (*f == '0') pad = '0';
			else if (*f == '^') upper = 1;
			else if (*f == '#') ;
			else break;
		}
		while ((unsigned)*f - '0' < 10)
			width = width * 10 + (*f++ - '0');
		if (*f == 'E' || *f == 'O')
			f++;
		/* numeric helper: default padding and width per conversion */
#define NUM(v, w, p) put_num(b, (v), pad == '-' ? 0 : width ? width : (w), pad == '-' ? 0 : pad ? pad : (p))
		const char *str = 0;
		char tmp[16];
		long long iy;
		switch (*f) {
		case 'a': str = (unsigned)tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "?"; goto abbr;
		case 'A': str = (unsigned)tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "?"; goto full;
		case 'b': case 'h': str = (unsigned)tm->tm_mon < 12 ? mon_name[tm->tm_mon] : "?"; goto abbr;
		case 'B': str = (unsigned)tm->tm_mon < 12 ? mon_name[tm->tm_mon] : "?"; goto full;
		case 'c': fmt_tm(b, "%a %b %e %H:%M:%S %Y", tm); break;
		case 'C': NUM((tm->tm_year + 1900LL) / 100 - ((tm->tm_year + 1900LL) % 100 < 0), 2, '0'); break;
		case 'd': NUM(tm->tm_mday, 2, '0'); break;
		case 'D': fmt_tm(b, "%m/%d/%y", tm); break;
		case 'e': NUM(tm->tm_mday, 2, ' '); break;
		case 'F': fmt_tm(b, "%Y-%m-%d", tm); break;
		case 'g': iso_week(tm, &iy); NUM(((iy % 100) + 100) % 100, 2, '0'); break;
		case 'G': iso_week(tm, &iy); NUM(iy, 0, '0'); break;
		case 'H': NUM(tm->tm_hour, 2, '0'); break;
		case 'I': NUM(tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, 2, '0'); break;
		case 'j': NUM(tm->tm_yday + 1, 3, '0'); break;
		case 'k': NUM(tm->tm_hour, 2, ' '); break;
		case 'l': NUM(tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, 2, ' '); break;
		case 'm': NUM(tm->tm_mon + 1, 2, '0'); break;
		case 'M': NUM(tm->tm_min, 2, '0'); break;
		case 'n': put(b, "\n", 1); break;
		case 'p': put(b, tm->tm_hour < 12 ? "AM" : "PM", 2); break;
		case 'P': put(b, tm->tm_hour < 12 ? "am" : "pm", 2); break;
		case 'r': fmt_tm(b, "%I:%M:%S %p", tm); break;
		case 'R': fmt_tm(b, "%H:%M", tm); break;
		case 's': {
			struct tm c = *tm;
			NUM((long long)mktime(&c), 0, 0);
			break;
		}
		case 'S': NUM(tm->tm_sec, 2, '0'); break;
		case 't': put(b, "\t", 1); break;
		case 'T': fmt_tm(b, "%H:%M:%S", tm); break;
		case 'u': NUM(tm->tm_wday ? tm->tm_wday : 7, 1, '0'); break;
		case 'U': NUM((tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0'); break;
		case 'V': NUM(iso_week(tm, &iy), 2, '0'); break;
		case 'w': NUM(tm->tm_wday, 1, '0'); break;
		case 'W': NUM((tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0'); break;
		case 'x': fmt_tm(b, "%m/%d/%y", tm); break;
		case 'X': fmt_tm(b, "%H:%M:%S", tm); break;
		case 'y': NUM(((tm->tm_year + 1900LL) % 100 + 100) % 100, 2, '0'); break;
		case 'Y': NUM(tm->tm_year + 1900LL, 0, '0'); break;
		case 'z': {
			long off = tm->tm_gmtoff;
			tmp[0] = off < 0 ? '-' : '+';
			if (off < 0)
				off = -off;
			snprintf(tmp + 1, sizeof tmp - 1, "%02ld%02ld", off / 3600 % 100, off / 60 % 60);
			put(b, tmp, strlen(tmp));
			break;
		}
		case 'Z':
			str = tm->tm_zone ? tm->tm_zone : "";
			goto full;
		case '%': put(b, "%", 1); break;
		case 0: return b->n;
		default:
			/* unknown: copy it through */
			put(b, f - 1, 2);
			break;
		}
		continue;
	abbr:
		memcpy(tmp, str, 3);
		tmp[3] = 0;
		str = tmp;
	full: {
			size_t l = strlen(str);
			for (size_t i = l; (int)i < width; i++)
				put(b, " ", 1);
			if (upper) {
				for (size_t i = 0; i < l; i++) {
					char c = (char)toupper((unsigned char)str[i]);
					put(b, &c, 1);
				}
			} else {
				put(b, str, l);
			}
		}
#undef NUM
	}
	return b->n;
}

/* ---- strptime ---- */

static int match_name(const char **ps, const char *const *names, int n, int *out)
{
	for (int i = 0; i < n; i++) {
		size_t full = strlen(names[i]);
		if (!strncasecmp(*ps, names[i], full)) {
			*ps += full;
			*out = i;
			return 0;
		}
		if (!strncasecmp(*ps, names[i], 3)) {
			*ps += 3;
			*out = i;
			return 0;
		}
	}
	return -1;
}

static int get_num(const char **ps, int maxdig, long lo, long hi, long *out)
{
	const char *s = *ps;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';
	long v = 0;
	int d = 0;
	for (; d < maxdig && (unsigned)*s - '0' < 10; d++)
		v = v * 10 + (*s++ - '0');
	if (!d)
		return -1;
	if (neg)
		v = -v;
	if (v < lo || v > hi)
		return -1;
	*ps = s;
	*out = v;
	return 0;
}

char *strptime(const char *__restrict s0, const char *__restrict f, struct tm *__restrict tm)
{
	const char *s = s0;
	int pm = -1, century = -1, have_yy = 0;
	long v;
	for (; *f; f++) {
		if (isspace((unsigned char)*f)) {
			while (isspace((unsigned char)*s))
				s++;
			continue;
		}
		if (*f != '%') {
			if (*s++ != *f)
				return 0;
			continue;
		}
		f++;
		if (*f == 'E' || *f == 'O')
			f++;
		if (*f != 'n' && *f != 't' && *f != '%')
			while (isspace((unsigned char)*s))
				s++;
		switch (*f) {
		case 'a': case 'A':
			if (match_name(&s, wday_name, 7, &tm->tm_wday))
				return 0;
			break;
		case 'b': case 'B': case 'h':
			if (match_name(&s, mon_name, 12, &tm->tm_mon))
				return 0;
			break;
		case 'c':
			if (!(s = strptime(s, "%a %b %e %H:%M:%S %Y", tm)))
				return 0;
			break;
		case 'C':
			if (get_num(&s, 2, 0, 99, &v))
				return 0;
			century = (int)v;
			break;
		case 'd': case 'e':
			if (get_num(&s, 2, 1, 31, &v))
				return 0;
			tm->tm_mday = (int)v;
			break;
		case 'D': case 'x':
			if (!(s = strptime(s, "%m/%d/%y", tm)))
				return 0;
			break;
		case 'F':
			if (!(s = strptime(s, "%Y-%m-%d", tm)))
				return 0;
			break;
		case 'H': case 'k':
			if (get_num(&s, 2, 0, 23, &v))
				return 0;
			tm->tm_hour = (int)v;
			break;
		case 'I': case 'l':
			if (get_num(&s, 2, 1, 12, &v))
				return 0;
			tm->tm_hour = (int)v % 12;
			break;
		case 'j':
			if (get_num(&s, 3, 1, 366, &v))
				return 0;
			tm->tm_yday = (int)v - 1;
			break;
		case 'm':
			if (get_num(&s, 2, 1, 12, &v))
				return 0;
			tm->tm_mon = (int)v - 1;
			break;
		case 'M':
			if (get_num(&s, 2, 0, 59, &v))
				return 0;
			tm->tm_min = (int)v;
			break;
		case 'n': case 't':
			while (isspace((unsigned char)*s))
				s++;
			break;
		case 'p':
			if (!strncasecmp(s, "AM", 2))
				pm = 0;
			else if (!strncasecmp(s, "PM", 2))
				pm = 1;
			else
				return 0;
			s += 2;
			break;
		case 'r':
			if (!(s = strptime(s, "%I:%M:%S %p", tm)))
				return 0;
			break;
		case 'R':
			if (!(s = strptime(s, "%H:%M", tm)))
				return 0;
			break;
		case 's': {
			char *e;
			errno = 0;
			long long t = strtoll(s, &e, 10);
			if (e == s || errno)
				return 0;
			time_t tt = (time_t)t;
			if (!localtime_r(&tt, tm))
				return 0;
			s = e;
			break;
		}
		case 'S':
			if (get_num(&s, 2, 0, 61, &v))
				return 0;
			tm->tm_sec = (int)v;
			break;
		case 'T': case 'X':
			if (!(s = strptime(s, "%H:%M:%S", tm)))
				return 0;
			break;
		case 'u':
			if (get_num(&s, 1, 1, 7, &v))
				return 0;
			tm->tm_wday = (int)v % 7;
			break;
		case 'w':
			if (get_num(&s, 1, 0, 6, &v))
				return 0;
			tm->tm_wday = (int)v;
			break;
		case 'U': case 'W': case 'V':
			if (get_num(&s, 2, 0, 53, &v))
				return 0;
			break;
		case 'y':
			if (get_num(&s, 2, 0, 99, &v))
				return 0;
			have_yy = 1;
			/* POSIX: 69-99 are 1969-1999, 00-68 are 2000-2068 */
			tm->tm_year = (int)(v < 69 ? v + 100 : v);
			break;
		case 'Y':
			if (get_num(&s, 9, -99999999, 999999999, &v))
				return 0;
			tm->tm_year = (int)(v - 1900);
			break;
		case 'z': {
			if (*s == 'Z') {
				tm->tm_gmtoff = 0;
				s++;
				break;
			}
			int neg = *s == '-';
			if (*s != '+' && *s != '-')
				return 0;
			s++;
			long hh, mm = 0;
			if (get_num(&s, 2, 0, 24, &hh))
				return 0;
			if (*s == ':')
				s++;
			get_num(&s, 2, 0, 59, &mm);
			tm->tm_gmtoff = (hh * 3600 + mm * 60) * (neg ? -1 : 1);
			break;
		}
		case 'Z':
			while (isalpha((unsigned char)*s))
				s++;
			break;
		case '%':
			if (*s++ != '%')
				return 0;
			break;
		default:
			return 0;
		}
	}
	if (pm == 1 && tm->tm_hour < 12)
		tm->tm_hour += 12;
	if (century >= 0) {
		int yy = have_yy ? (tm->tm_year + 1900) % 100 : 0;
		tm->tm_year = century * 100 + yy - 1900;
	}
	return (char *)s;
}
