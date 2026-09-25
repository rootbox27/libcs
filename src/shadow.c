/* <shadow.h>: /etc/shadow parsing. Buffers holding password hashes are
 * wiped before they are reused or freed. */
#include <shadow.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include "internal.h"

static long num(char *s)
{
	if (!*s)
		return -1;
	char *e;
	errno = 0;
	long v = strtol(s, &e, 10);
	if (*e || errno || v < 0)
		return -2;
	return v;
}

/* parse one line (without newline) in place into sp; 0 on success */
static int parse(char *line, struct spwd *sp)
{
	char *f[9];
	int n = 0;
	for (char *p = line; n < 9; n++) {
		f[n] = p;
		p = strchr(p, ':');
		if (!p) {
			n++;
			break;
		}
		*p++ = 0;
	}
	if (n < 2)
		return -1;
	for (int i = n; i < 9; i++)
		f[i] = (char *)"";
	sp->sp_namp = f[0];
	sp->sp_pwdp = f[1];
	long *v[6] = { &sp->sp_lstchg, &sp->sp_min, &sp->sp_max, &sp->sp_warn, &sp->sp_inact, &sp->sp_expire };
	for (int i = 0; i < 6; i++)
		if ((*v[i] = num(f[2 + i])) == -2)
			return -1;
	char *e;
	sp->sp_flag = *f[8] ? strtoul(f[8], &e, 10) : (unsigned long)-1;
	return 0;
}

int sgetspent_r(const char *s, struct spwd *sp, char *buf, size_t len, struct spwd **res)
{
	*res = 0;
	size_t n = strcspn(s, "\n");
	if (n >= len)
		return ERANGE;
	memcpy(buf, s, n);
	buf[n] = 0;
	if (parse(buf, sp))
		return ENOENT;
	*res = sp;
	return 0;
}

int fgetspent_r(FILE *f, struct spwd *sp, char *buf, size_t len, struct spwd **res)
{
	*res = 0;
	for (;;) {
		if (!fgets(buf, (int)(len > INT_MAX ? INT_MAX : len), f))
			return ENOENT;
		size_t n = strlen(buf);
		if (n && buf[n - 1] == '\n')
			buf[--n] = 0;
		else if (!feof(f))
			return ERANGE;
		if (!n || buf[0] == '#')
			continue;
		if (!parse(buf, sp)) {
			*res = sp;
			return 0;
		}
	}
}

static FILE *db;
static struct spwd ent;
static char ent_buf[1024];
static volatile int lock;

void setspent(void)
{
	LOCK(lock);
	if (db)
		rewind(db);
	UNLOCK(lock);
}

void endspent(void)
{
	LOCK(lock);
	if (db)
		fclose(db);
	db = 0;
	explicit_bzero(ent_buf, sizeof ent_buf);
	UNLOCK(lock);
}

int getspent_r(struct spwd *sp, char *buf, size_t len, struct spwd **res)
{
	*res = 0;
	LOCK(lock);
	if (!db)
		db = fopen(SHADOW, "re");
	int r = db ? fgetspent_r(db, sp, buf, len, res) : errno;
	UNLOCK(lock);
	return r;
}

struct spwd *getspent(void)
{
	struct spwd *r;
	explicit_bzero(ent_buf, sizeof ent_buf);
	errno = getspent_r(&ent, ent_buf, sizeof ent_buf, &r);
	return r;
}

struct spwd *fgetspent(FILE *f)
{
	struct spwd *r;
	explicit_bzero(ent_buf, sizeof ent_buf);
	errno = fgetspent_r(f, &ent, ent_buf, sizeof ent_buf, &r);
	return r;
}

struct spwd *sgetspent(const char *s)
{
	struct spwd *r;
	explicit_bzero(ent_buf, sizeof ent_buf);
	errno = sgetspent_r(s, &ent, ent_buf, sizeof ent_buf, &r);
	return r;
}

int getspnam_r(const char *name, struct spwd *sp, char *buf, size_t len, struct spwd **res)
{
	*res = 0;
	if (!*name || strchr(name, ':') || strchr(name, '\n'))
		return ENOENT;
	FILE *f = fopen(SHADOW, "re");
	if (!f)
		return errno;
	int r;
	while (!(r = fgetspent_r(f, sp, buf, len, res))) {
		if (!strcmp(sp->sp_namp, name))
			break;
		*res = 0;
	}
	fclose(f);
	if (r == ENOENT)
		r = 0; /* not found is not an error */
	if (!*res)
		explicit_bzero(buf, len);
	return r;
}

struct spwd *getspnam(const char *name)
{
	struct spwd *r;
	explicit_bzero(ent_buf, sizeof ent_buf);
	int e = getspnam_r(name, &ent, ent_buf, sizeof ent_buf, &r);
	if (e)
		errno = e;
	return r;
}

static int put_num(FILE *f, long v)
{
	return v == -1 ? fputs(":", f) : fprintf(f, "%ld:", v);
}

int putspent(const struct spwd *sp, FILE *f)
{
	if (strchr(sp->sp_namp, ':') || strchr(sp->sp_namp, '\n') ||
	    (sp->sp_pwdp && (strchr(sp->sp_pwdp, ':') || strchr(sp->sp_pwdp, '\n')))) {
		errno = EINVAL;
		return -1;
	}
	if (fprintf(f, "%s:%s:", sp->sp_namp, sp->sp_pwdp ? sp->sp_pwdp : "") < 0 || put_num(f, sp->sp_lstchg) < 0 ||
	    put_num(f, sp->sp_min) < 0 || put_num(f, sp->sp_max) < 0 || put_num(f, sp->sp_warn) < 0 ||
	    put_num(f, sp->sp_inact) < 0 || put_num(f, sp->sp_expire) < 0)
		return -1;
	if (sp->sp_flag != (unsigned long)-1 && fprintf(f, "%lu", sp->sp_flag) < 0)
		return -1;
	return fputs("\n", f) < 0 ? -1 : 0;
}

static int lock_fd = -1;

int lckpwdf(void)
{
	int fd = open("/etc/.pwd.lock", O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
	for (int i = 0; i < 15; i++) {
		if (fcntl(fd, F_SETLK, &fl) == 0) {
			lock_fd = fd;
			return 0;
		}
		sleep(1);
	}
	close(fd);
	errno = EAGAIN;
	return -1;
}

int ulckpwdf(void)
{
	if (lock_fd < 0)
		return -1;
	close(lock_fd);
	lock_fd = -1;
	return 0;
}
