/* Time zones: TZ parsing, TZif files, POSIX TZ rules, tzset.
 *
 * TZ unset: /etc/localtime. TZ="": UTC. ":name" or a name that is not a
 * valid POSIX rule: a TZif file, absolute or under /usr/share/zoneinfo.
 * In set-user-ID programs (AT_SECURE) only relative names without ".."
 * are accepted. TZif files are untrusted input: size-limited and fully
 * bounds-checked; anything malformed falls back to UTC. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "tz.h"

#define ZONEINFO "/usr/share/zoneinfo/"
#define MAX_TZFILE (1 << 20)
#define MAX_TIMES 4000
#define MAX_TYPES 256
#define MAX_ABBR 256

struct rule_date {
	int kind;          /* 'J' Julian no-leap 1..365, 'D' 0..365, 'M' month.week.day */
	int n, m, w, d;
	long secs;         /* time of day (local), may exceed a day */
};

struct posix_rule {
	int valid;
	char std[16], dst[16];
	long std_off, dst_off;  /* seconds east of UTC */
	int has_dst;
	struct rule_date start, end;
};

struct type {
	int32_t off;
	unsigned char isdst, abbr;
};

static struct {
	char key[256];          /* TZ value this zone was loaded for */
	int loaded;
	int ntimes, ntypes;
	int64_t times[MAX_TIMES];
	unsigned char idx[MAX_TIMES];
	struct type types[MAX_TYPES];
	char abbr[MAX_ABBR + 1];
	struct posix_rule rule; /* footer, or the TZ string itself */
} Z;

static volatile int tz_lock;

char *tzname[2] = { (char *)"UTC", (char *)"UTC" };
long timezone;
int daylight;

/* ---- POSIX TZ strings ---- */

static int parse_name(const char **ps, char *out)
{
	const char *s = *ps;
	size_t n = 0;
	if (*s == '<') {
		s++;
		while (*s && *s != '>') {
			if (n + 1 >= 16)
				return -1;
			out[n++] = *s++;
		}
		if (*s++ != '>')
			return -1;
	} else {
		while ((unsigned)(*s | 32) - 'a' < 26) {
			if (n + 1 >= 16)
				return -1;
			out[n++] = *s++;
		}
	}
	if (n < 3)
		return -1;
	out[n] = 0;
	*ps = s;
	return 0;
}

/* [+-]hh[:mm[:ss]], hours up to 167 (for rule times) */
static int parse_hms(const char **ps, long *out)
{
	const char *s = *ps;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';
	if ((unsigned)*s - '0' >= 10)
		return -1;
	long h = 0, m = 0, sec = 0;
	while ((unsigned)*s - '0' < 10 && h < 1000)
		h = h * 10 + (*s++ - '0');
	if (*s == ':') {
		s++;
		if ((unsigned)*s - '0' >= 10)
			return -1;
		while ((unsigned)*s - '0' < 10 && m < 100)
			m = m * 10 + (*s++ - '0');
		if (*s == ':') {
			s++;
			if ((unsigned)*s - '0' >= 10)
				return -1;
			while ((unsigned)*s - '0' < 10 && sec < 100)
				sec = sec * 10 + (*s++ - '0');
		}
	}
	if (h > 167 || m > 59 || sec > 59)
		return -1;
	*out = (h * 3600 + m * 60 + sec) * (neg ? -1 : 1);
	*ps = s;
	return 0;
}

static int parse_num(const char **ps, int lo, int hi, int *out)
{
	const char *s = *ps;
	int n = 0;
	if ((unsigned)*s - '0' >= 10)
		return -1;
	while ((unsigned)*s - '0' < 10 && n <= hi)
		n = n * 10 + (*s++ - '0');
	if (n < lo || n > hi)
		return -1;
	*out = n;
	*ps = s;
	return 0;
}

static int parse_date(const char **ps, struct rule_date *d)
{
	const char *s = *ps;
	if (*s == 'J') {
		s++;
		d->kind = 'J';
		if (parse_num(&s, 1, 365, &d->n))
			return -1;
	} else if (*s == 'M') {
		s++;
		d->kind = 'M';
		if (parse_num(&s, 1, 12, &d->m) || *s++ != '.' || parse_num(&s, 1, 5, &d->w) ||
		    *s++ != '.' || parse_num(&s, 0, 6, &d->d))
			return -1;
	} else {
		d->kind = 'D';
		if (parse_num(&s, 0, 365, &d->n))
			return -1;
	}
	d->secs = 7200;
	if (*s == '/') {
		s++;
		if (parse_hms(&s, &d->secs))
			return -1;
	}
	*ps = s;
	return 0;
}

static int parse_posix(const char *s, struct posix_rule *r)
{
	memset(r, 0, sizeof *r);
	long off;
	if (parse_name(&s, r->std) || parse_hms(&s, &off) || off > 24 * 3600 || off < -24 * 3600)
		return -1;
	r->std_off = -off; /* POSIX offsets are west-positive */
	if (*s) {
		if (parse_name(&s, r->dst))
			return -1;
		r->has_dst = 1;
		r->dst_off = r->std_off + 3600;
		if (*s && *s != ',') {
			if (parse_hms(&s, &off) || off > 24 * 3600 || off < -24 * 3600)
				return -1;
			r->dst_off = -off;
		}
		if (*s == ',') {
			s++;
			if (parse_date(&s, &r->start) || *s++ != ',' || parse_date(&s, &r->end))
				return -1;
		} else {
			/* default: US rules */
			r->start = (struct rule_date){ 'M', 0, 3, 2, 0, 7200 };
			r->end = (struct rule_date){ 'M', 0, 11, 1, 0, 7200 };
		}
	}
	if (*s)
		return -1;
	r->valid = 1;
	return 0;
}

/* ---- calendar helpers (also used by the conversion code) ---- */

hidden int __is_leap(long long y)
{
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* Days from 1970-01-01 to y-m-d (proleptic Gregorian; m is 1..12). */
hidden long long __days_from_civil(long long y, unsigned m, unsigned d)
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

/* Seconds from the start of `year` (UTC) to the rule date, as local time. */
static long long rule_time(long long year, const struct rule_date *d)
{
	long long day;
	if (d->kind == 'J') {
		/* 1..365, February 29 is never counted */
		day = d->n - 1;
		if (__is_leap(year) && d->n >= 60)
			day++;
	} else if (d->kind == 'D') {
		day = d->n;
	} else {
		/* d-th weekday of week w (5 = last) of month m */
		long long first = __days_from_civil(year, (unsigned)d->m, 1);
		long long jan1 = __days_from_civil(year, 1, 1);
		int wd_first = (int)((first % 7 + 7 + 4) % 7); /* 1970-01-01 was a Thursday */
		int delta = (d->d - wd_first + 7) % 7;
		int mday = 1 + delta + 7 * (d->w - 1);
		static const int mlen[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		int len = mlen[d->m - 1] + (d->m == 2 && __is_leap(year));
		while (mday > len)
			mday -= 7;
		day = first - jan1 + mday - 1;
	}
	return day * 86400 + d->secs;
}

/* UTC offset and DST flag at UTC time t under a POSIX rule. */
static void rule_lookup(const struct posix_rule *r, long long t, long *off, int *isdst, const char **name)
{
	*off = r->std_off;
	*isdst = 0;
	*name = r->std;
	if (!r->has_dst)
		return;
	/* the year of t in standard local time */
	long long days = (t + r->std_off) / 86400 - ((t + r->std_off) % 86400 < 0);
	/* 400 Gregorian years are 146097 days: this is within a year */
	long long y = 1970 + (days >= 0 ? days * 400 / 146097 : (days * 400 - 146096) / 146097);
	while (__days_from_civil(y + 1, 1, 1) <= days)
		y++;
	while (__days_from_civil(y, 1, 1) > days)
		y--;
	/* Like glibc, rule transitions start in 1970; earlier times keep the
	 * state before the first one (standard time for northern rules, DST
	 * for southern rules whose first 1970 transition ends DST). */
	int pre = y < 1970;
	if (pre)
		y = 1970;
	long long y0 = __days_from_civil(y, 1, 1) * 86400;
	/* transitions in UTC: start is given in standard time, end in DST */
	long long start = y0 + rule_time(y, &r->start) - r->std_off;
	long long end = y0 + rule_time(y, &r->end) - r->dst_off;
	int in_dst = pre ? start > end : start < end ? (t >= start && t < end) : !(t >= end && t < start);
	if (in_dst) {
		*off = r->dst_off;
		*isdst = 1;
		*name = r->dst;
	}
}

/* ---- TZif ---- */

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int64_t be64(const unsigned char *p)
{
	return (int64_t)((uint64_t)be32(p) << 32 | be32(p + 4));
}

static int load_tzif(const char *path)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOCTTY);
	if (fd < 0)
		return -1;
	struct stat st;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 44 || st.st_size > MAX_TZFILE) {
		close(fd);
		return -1;
	}
	size_t len = (size_t)st.st_size;
	unsigned char *buf = malloc(len + 1);
	if (!buf) {
		close(fd);
		return -1;
	}
	size_t got = 0;
	while (got < len) {
		ssize_t r = read(fd, buf + got, len - got);
		if (r <= 0)
			break;
		got += (size_t)r;
	}
	close(fd);
	int rc = -1;
	if (got != len || memcmp(buf, "TZif", 4))
		goto out;

	const unsigned char *p = buf, *end = buf + len;
	int version = buf[4];
	int pass64 = 0;
	for (;;) {
		if (end - p < 44)
			goto out;
		uint32_t isutc = be32(p + 20), isstd = be32(p + 24), leap = be32(p + 28);
		uint32_t ntimes = be32(p + 32), ntypes = be32(p + 36), nchars = be32(p + 40);
		int tsz = pass64 ? 8 : 4;
		size_t body = (size_t)ntimes * (size_t)tsz + ntimes + (size_t)ntypes * 6 + nchars +
		              (size_t)leap * (size_t)(tsz + 4) + isstd + isutc;
		if (ntimes > MAX_TIMES || ntypes == 0 || ntypes > MAX_TYPES || nchars > MAX_ABBR ||
		    leap > 10000 || isstd > ntypes || isutc > ntypes || body > (size_t)(end - p - 44))
			goto out;
		const unsigned char *q = p + 44;
		if (version >= '2' && !pass64) {
			p = q + body;
			pass64 = 1;
			continue;
		}
		Z.ntimes = (int)ntimes;
		Z.ntypes = (int)ntypes;
		for (uint32_t i = 0; i < ntimes; i++, q += tsz)
			Z.times[i] = pass64 ? be64(q) : (int32_t)be32(q);
		for (uint32_t i = 0; i < ntimes; i++) {
			if (q[i] >= ntypes)
				goto out;
			Z.idx[i] = q[i];
		}
		q += ntimes;
		for (uint32_t i = 0; i < ntypes; i++, q += 6) {
			int32_t off = (int32_t)be32(q);
			if (off < -25 * 3600 || off > 26 * 3600 || q[4] > 1 || q[5] >= nchars)
				goto out;
			Z.types[i] = (struct type){ off, q[4], q[5] };
		}
		memcpy(Z.abbr, q, nchars);
		Z.abbr[nchars] = 0;
		Z.abbr[MAX_ABBR] = 0;
		for (int i = 1; i < Z.ntimes; i++)
			if (Z.times[i] <= Z.times[i - 1])
				goto out;
		q += nchars + (size_t)leap * (size_t)(tsz + 4) + isstd + isutc;
		/* v2+ footer: "\n<POSIX TZ>\n" */
		Z.rule.valid = 0;
		if (pass64 && q < end && *q == '\n') {
			const unsigned char *nl = memchr(q + 1, '\n', (size_t)(end - q - 1));
			char tmp[128];
			if (nl && (size_t)(nl - q - 1) < sizeof tmp) {
				memcpy(tmp, q + 1, (size_t)(nl - q - 1));
				tmp[nl - q - 1] = 0;
				if (tmp[0] && parse_posix(tmp, &Z.rule))
					Z.rule.valid = 0;
			}
		}
		rc = 0;
		break;
	}
out:
	free(buf);
	return rc;
}

static void set_utc(void)
{
	Z.ntimes = 0;
	Z.ntypes = 0;
	memset(&Z.rule, 0, sizeof Z.rule);
	strcpy(Z.rule.std, "UTC");
	Z.rule.valid = 1;
}

static int safe_name(const char *name)
{
	if (!__libc.secure)
		return 1;
	if (name[0] == '/')
		return 0;
	for (const char *p = name; *p; p++)
		if (p[0] == '.' && p[1] == '.' && (p == name || p[-1] == '/') && (p[2] == '/' || !p[2]))
			return 0;
	return 1;
}

static void do_tzset(void)
{
	const char *tz = getenv("TZ");
	const char *key = tz ? tz : "\x01unset";
	if (Z.loaded && !strcmp(Z.key, key))
		return;
	Z.loaded = 1;
	strlcpy(Z.key, key, sizeof Z.key);

	if (tz && !*tz) {
		set_utc();
	} else if (tz && *tz != ':' && !parse_posix(tz, &Z.rule)) {
		Z.ntimes = Z.ntypes = 0;
	} else {
		const char *name = !tz ? "/etc/localtime" : *tz == ':' ? tz + 1 : tz;
		char path[PATH_MAX];
		int ok = 0;
		if (tz && !safe_name(name)) {
			ok = 0;
		} else if (name[0] == '/') {
			ok = !load_tzif(name);
		} else if (strlen(name) + sizeof ZONEINFO <= sizeof path) {
			memcpy(path, ZONEINFO, sizeof ZONEINFO - 1);
			strcpy(path + sizeof ZONEINFO - 1, name);
			ok = !load_tzif(path);
		}
		if (!ok)
			set_utc();
	}

	/* tzname/timezone/daylight: for a zone file, from its local time
	 * types in transition order (so the most recent names win and any
	 * historical DST sets daylight, as glibc does); else from the rule. */
	if (Z.ntypes) {
		const struct type *t0 = &Z.types[0];
		tzname[0] = tzname[1] = Z.abbr + t0->abbr;
		timezone = -t0->off;
		daylight = 0;
		for (int i = -1; i < Z.ntimes; i++) {
			const struct type *t = i < 0 ? t0 : &Z.types[Z.idx[i]];
			if (t->isdst) {
				tzname[1] = Z.abbr + t->abbr;
				daylight = 1;
			} else {
				tzname[0] = Z.abbr + t->abbr;
				timezone = -t->off;
			}
		}
		if (!daylight)
			tzname[1] = tzname[0];
	} else if (Z.rule.valid) {
		tzname[0] = Z.rule.std;
		tzname[1] = Z.rule.has_dst ? Z.rule.dst : Z.rule.std;
		timezone = -Z.rule.std_off;
		daylight = Z.rule.has_dst;
	}
}

void tzset(void)
{
	LOCK(tz_lock);
	do_tzset();
	UNLOCK(tz_lock);
}

/* Offset, DST flag and abbreviation at UTC time t. The name points into
 * static zone data, valid until TZ changes. */
hidden void __tz_lookup(long long t, long *off, int *isdst, const char **name)
{
	LOCK(tz_lock);
	do_tzset();
	if (Z.ntimes && (t < Z.times[Z.ntimes - 1] || !Z.rule.valid)) {
		const struct type *ty;
		if (t < Z.times[0]) {
			/* before the first transition: first non-DST type */
			int k = 0;
			while (k < Z.ntypes && Z.types[k].isdst)
				k++;
			ty = &Z.types[k < Z.ntypes ? k : 0];
		} else {
			int lo = 0, hi = Z.ntimes - 1;
			while (lo < hi) {
				int mid = (lo + hi + 1) / 2;
				if (Z.times[mid] <= t)
					lo = mid;
				else
					hi = mid - 1;
			}
			ty = &Z.types[Z.idx[lo]];
		}
		*off = ty->off;
		*isdst = ty->isdst;
		*name = Z.abbr + ty->abbr;
	} else if (Z.rule.valid) {
		rule_lookup(&Z.rule, t, off, isdst, name);
	} else if (Z.ntypes) {
		*off = Z.types[0].off;
		*isdst = Z.types[0].isdst;
		*name = Z.abbr + Z.types[0].abbr;
	} else {
		*off = 0;
		*isdst = 0;
		*name = "UTC";
	}
	UNLOCK(tz_lock);
}
