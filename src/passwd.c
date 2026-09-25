/* User and group databases from /etc/passwd and /etc/group.
 *
 * Lines are parsed strictly: a wrong field count or a malformed number
 * skips the line, and NIS compatibility entries ("+", "-") are ignored.
 * getlogin uses the kernel's audit login uid rather than environment
 * variables, which the caller controls. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PASSWD "/etc/passwd"
#define GROUP "/etc/group"

/* Split s at ':' into exactly n fields. */
static int split(char *s, char **f, int n)
{
	for (int i = 0; i < n; i++) {
		f[i] = s;
		char *c = strchr(s, ':');
		if (i == n - 1) {
			if (c)
				return -1;
			break;
		}
		if (!c)
			return -1;
		*c = 0;
		s = c + 1;
	}
	return 0;
}

static int parse_id(const char *s, unsigned *out)
{
	if ((unsigned)*s - '0' >= 10)
		return -1;
	char *e;
	errno = 0;
	unsigned long v = strtoul(s, &e, 10);
	if (*e || errno || v > UINT_MAX - 1) /* (uid_t)-1 is not a valid id */
		return -1;
	*out = (unsigned)v;
	return 0;
}

static FILE *open_db(const char *path)
{
	return fopen(path, "re");
}

/* Read the next well-formed line; the line buffer is reused. */
static char *next_line(FILE *f, char **line, size_t *cap)
{
	ssize_t n;
	while ((n = getline(line, cap, f)) >= 0) {
		if (n && (*line)[n - 1] == '\n')
			(*line)[--n] = 0;
		if ((size_t)n != strlen(*line))
			continue; /* embedded NUL */
		if (!n || **line == '+' || **line == '-' || **line == '#')
			continue;
		return *line;
	}
	return 0;
}

/* ---- passwd ---- */

static int parse_pw(char *line, char **f, unsigned *uid, unsigned *gid)
{
	return split(line, f, 7) || !*f[0] || parse_id(f[2], uid) || parse_id(f[3], gid) ? -1 : 0;
}

/* Copy the fields into buf and fill pw. */
static int fill_pw(char **f, unsigned uid, unsigned gid, struct passwd *pw, char *buf, size_t len)
{
	size_t need = 0, l[7];
	for (int i = 0; i < 7; i++)
		need += (l[i] = strlen(f[i])) + 1;
	if (need > len)
		return ERANGE;
	char *p = buf;
	char *out[7];
	for (int i = 0; i < 7; i++) {
		out[i] = p;
		memcpy(p, f[i], l[i] + 1);
		p += l[i] + 1;
	}
	pw->pw_name = out[0];
	pw->pw_passwd = out[1];
	pw->pw_uid = uid;
	pw->pw_gid = gid;
	pw->pw_gecos = out[4];
	pw->pw_dir = out[5];
	pw->pw_shell = out[6];
	return 0;
}

static int getpw_r(const char *name, uid_t uid, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	*res = 0;
	int e = errno;
	FILE *f = open_db(PASSWD);
	if (!f)
		return errno == ENOENT ? (errno = e, 0) : errno;
	char *line = 0, *fl[7];
	size_t cap = 0;
	unsigned u, g;
	int r = 0;
	while (next_line(f, &line, &cap)) {
		if (parse_pw(line, fl, &u, &g))
			continue;
		if (name ? strcmp(fl[0], name) : u != uid)
			continue;
		r = fill_pw(fl, u, g, pw, buf, len);
		if (!r)
			*res = pw;
		break;
	}
	free(line);
	fclose(f);
	errno = e;
	return r;
}

int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	return getpw_r(name, 0, pw, buf, len, res);
}

int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
	return getpw_r(0, uid, pw, buf, len, res);
}

static struct passwd pw_static;
static char *pw_buf;
static size_t pw_len;

/* Non-reentrant forms: grow the static buffer on ERANGE. */
static struct passwd *getpw(const char *name, uid_t uid)
{
	struct passwd *res;
	for (;;) {
		if (!pw_buf) {
			pw_len = 1024;
			if (!(pw_buf = malloc(pw_len)))
				return 0;
		}
		int r = getpw_r(name, uid, &pw_static, pw_buf, pw_len, &res);
		if (r != ERANGE) {
			if (r)
				errno = r;
			return res;
		}
		char *nb = pw_len < (1 << 20) ? realloc(pw_buf, pw_len * 2) : 0;
		if (!nb) {
			errno = ERANGE;
			return 0;
		}
		pw_buf = nb;
		pw_len *= 2;
	}
}

struct passwd *getpwnam(const char *name) { return getpw(name, 0); }
struct passwd *getpwuid(uid_t uid) { return getpw(0, uid); }

static FILE *pw_iter;
static char *pw_line;
static size_t pw_cap;

void setpwent(void)
{
	if (pw_iter)
		rewind(pw_iter);
}

void endpwent(void)
{
	if (pw_iter)
		fclose(pw_iter);
	pw_iter = 0;
}

struct passwd *getpwent(void)
{
	if (!pw_iter && !(pw_iter = open_db(PASSWD)))
		return 0;
	char *fl[7];
	unsigned u, g;
	while (next_line(pw_iter, &pw_line, &pw_cap)) {
		if (parse_pw(pw_line, fl, &u, &g))
			continue;
		/* the fields live in pw_line until the next call */
		pw_static = (struct passwd){ fl[0], fl[1], u, g, fl[4], fl[5], fl[6] };
		return &pw_static;
	}
	return 0;
}

/* ---- group ---- */

static int parse_gr(char *line, char **f, unsigned *gid)
{
	return split(line, f, 4) || !*f[0] || parse_id(f[2], gid) ? -1 : 0;
}

static int fill_gr(char **f, unsigned gid, struct group *gr, char *buf, size_t len)
{
	/* member list: count, then pointer array (aligned) and strings */
	size_t nmem = 0;
	if (*f[3]) {
		nmem = 1;
		for (const char *p = f[3]; *p; p++)
			nmem += *p == ',';
	}
	size_t align = (-(uintptr_t)buf) & (sizeof(char *) - 1);
	size_t ptrs = (nmem + 1) * sizeof(char *);
	size_t strs = strlen(f[0]) + strlen(f[1]) + strlen(f[3]) + 3;
	if (align + ptrs + strs > len)
		return ERANGE;
	char **mem = (char **)(buf + align);
	char *p = buf + align + ptrs;
	gr->gr_name = strcpy(p, f[0]);
	p += strlen(p) + 1;
	gr->gr_passwd = strcpy(p, f[1]);
	p += strlen(p) + 1;
	strcpy(p, f[3]);
	size_t k = 0;
	if (nmem) {
		for (char *m = p;; ) {
			char *c = strchr(m, ',');
			if (c)
				*c = 0;
			if (*m)
				mem[k++] = m;
			if (!c)
				break;
			m = c + 1;
		}
	}
	mem[k] = 0;
	gr->gr_mem = mem;
	gr->gr_gid = gid;
	return 0;
}

static int getgr_r(const char *name, gid_t gid, struct group *gr, char *buf, size_t len, struct group **res)
{
	*res = 0;
	int e = errno;
	FILE *f = open_db(GROUP);
	if (!f)
		return errno == ENOENT ? (errno = e, 0) : errno;
	char *line = 0, *fl[4];
	size_t cap = 0;
	unsigned g;
	int r = 0;
	while (next_line(f, &line, &cap)) {
		if (parse_gr(line, fl, &g))
			continue;
		if (name ? strcmp(fl[0], name) : g != gid)
			continue;
		r = fill_gr(fl, g, gr, buf, len);
		if (!r)
			*res = gr;
		break;
	}
	free(line);
	fclose(f);
	errno = e;
	return r;
}

int getgrnam_r(const char *name, struct group *gr, char *buf, size_t len, struct group **res)
{
	return getgr_r(name, 0, gr, buf, len, res);
}

int getgrgid_r(gid_t gid, struct group *gr, char *buf, size_t len, struct group **res)
{
	return getgr_r(0, gid, gr, buf, len, res);
}

static struct group gr_static;
static char *gr_buf;
static size_t gr_len;

static struct group *getgr(const char *name, gid_t gid)
{
	struct group *res;
	for (;;) {
		if (!gr_buf) {
			gr_len = 1024;
			if (!(gr_buf = malloc(gr_len)))
				return 0;
		}
		int r = getgr_r(name, gid, &gr_static, gr_buf, gr_len, &res);
		if (r != ERANGE) {
			if (r)
				errno = r;
			return res;
		}
		char *nb = gr_len < (1 << 22) ? realloc(gr_buf, gr_len * 2) : 0;
		if (!nb) {
			errno = ERANGE;
			return 0;
		}
		gr_buf = nb;
		gr_len *= 2;
	}
}

struct group *getgrnam(const char *name) { return getgr(name, 0); }
struct group *getgrgid(gid_t gid) { return getgr(0, gid); }

int initgroups(const char *user, gid_t group)
{
	gid_t list[65536 / 64];
	size_t n = 0;
	list[n++] = group;
	FILE *f = open_db(GROUP);
	if (f) {
		char *line = 0, *fl[4];
		size_t cap = 0;
		unsigned g;
		while (next_line(f, &line, &cap)) {
			if (parse_gr(line, fl, &g) || g == group)
				continue;
			for (char *m = fl[3]; *m; ) {
				size_t l = strcspn(m, ",");
				if (l == strlen(user) && !strncmp(m, user, l)) {
					if (n < sizeof list / sizeof *list)
						list[n++] = g;
					break;
				}
				m += l + (m[l] == ',');
			}
		}
		free(line);
		fclose(f);
	}
	return setgroups(n, list);
}

/* ---- login name ---- */

int getlogin_r(char *buf, size_t len)
{
	char tmp[16];
	int fd = open("/proc/self/loginuid", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return ENXIO;
	ssize_t n = read(fd, tmp, sizeof tmp - 1);
	close(fd);
	if (n <= 0)
		return ENXIO;
	tmp[n] = 0;
	unsigned uid;
	if (parse_id(tmp, &uid))
		return ENXIO; /* unset: 4294967295 */
	struct passwd pw, *res;
	char pbuf[1024];
	int r = getpwuid_r(uid, &pw, pbuf, sizeof pbuf, &res);
	if (r)
		return r;
	if (!res)
		return ENXIO;
	if (strlen(pw.pw_name) >= len)
		return ERANGE;
	strcpy(buf, pw.pw_name);
	return 0;
}

char *getlogin(void)
{
	static char name[LOGIN_NAME_MAX];
	int r = getlogin_r(name, sizeof name);
	if (r) {
		errno = r;
		return 0;
	}
	return name;
}
