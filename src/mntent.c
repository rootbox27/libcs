/* <mntent.h>: fstab/mtab parsing. Fields may contain \ooo octal escapes
 * (\040 for a space). */
#include <mntent.h>
#include <string.h>
#include <stdlib.h>

FILE *setmntent(const char *path, const char *mode)
{
	/* close-on-exec, like every stream the library opens itself */
	char m[8];
	size_t n = strlen(mode);
	if (n > 5)
		return 0;
	memcpy(m, mode, n);
	m[n] = 'e';
	m[n + 1] = 0;
	return fopen(path, m);
}

int endmntent(FILE *f)
{
	if (f)
		fclose(f);
	return 1;
}

static char *unescape(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++) {
		if (p[0] == '\\' && p[1] >= '0' && p[1] <= '3' && p[2] >= '0' && p[2] <= '7' && p[3] >= '0' && p[3] <= '7') {
			*o++ = (char)((p[1] - '0') << 6 | (p[2] - '0') << 3 | (p[3] - '0'));
			p += 3;
		} else {
			*o++ = *p;
		}
	}
	*o = 0;
	return s;
}

struct mntent *getmntent_r(FILE *f, struct mntent *m, char *buf, int len)
{
	for (;;) {
		if (!fgets(buf, len, f))
			return 0;
		size_t n = strlen(buf);
		if (n && buf[n - 1] != '\n' && !feof(f)) {
			/* overlong line: skip the rest of it */
			int c;
			while ((c = getc(f)) != EOF && c != '\n')
				;
			continue;
		}
		char *save, *fld[6] = { 0 };
		int k = 0;
		char *p = buf + strspn(buf, " \t");
		if (*p == '#' || *p == '\n' || !*p)
			continue;
		for (char *t = strtok_r(p, " \t\n", &save); t && k < 6; t = strtok_r(0, " \t\n", &save))
			fld[k++] = t;
		if (k < 4)
			continue;
		m->mnt_fsname = unescape(fld[0]);
		m->mnt_dir = unescape(fld[1]);
		m->mnt_type = unescape(fld[2]);
		m->mnt_opts = unescape(fld[3]);
		m->mnt_freq = fld[4] ? atoi(fld[4]) : 0;
		m->mnt_passno = fld[5] ? atoi(fld[5]) : 0;
		return m;
	}
}

struct mntent *getmntent(FILE *f)
{
	static struct mntent m;
	static char buf[4096];
	return getmntent_r(f, &m, buf, sizeof buf);
}

static int put_escaped(FILE *f, const char *s)
{
	for (; *s; s++) {
		if (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\\') {
			if (fprintf(f, "\\%03o", (unsigned char)*s) < 0)
				return -1;
		} else if (putc(*s, f) == EOF) {
			return -1;
		}
	}
	return 0;
}

int addmntent(FILE *f, const struct mntent *m)
{
	if (fseek(f, 0, SEEK_END) < 0)
		return 1;
	if (put_escaped(f, m->mnt_fsname) || putc(' ', f) == EOF || put_escaped(f, m->mnt_dir) ||
	    putc(' ', f) == EOF || put_escaped(f, m->mnt_type) || putc(' ', f) == EOF ||
	    put_escaped(f, m->mnt_opts) || fprintf(f, " %d %d\n", m->mnt_freq, m->mnt_passno) < 0)
		return 1;
	return 0;
}

char *hasmntopt(const struct mntent *m, const char *opt)
{
	size_t n = strlen(opt);
	for (char *p = m->mnt_opts; p && *p;) {
		if (!strncmp(p, opt, n) && (p[n] == ',' || p[n] == '=' || !p[n]))
			return p;
		p = strchr(p, ',');
		if (p)
			p++;
	}
	return 0;
}
