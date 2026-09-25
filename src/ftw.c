/* ftw and nftw. One directory stream is open per level of the walk.
 * When symbolic links are followed, each directory is reported once:
 * later links to it (including loops) are skipped. */
#include <ftw.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct anc {
	const struct anc *up;
	dev_t dev;
	ino_t ino;
};

struct walk {
	struct anc *seen; /* directories visited, when following links */
	size_t nseen, cap;
	int (*nfn)(const char *, const struct stat *, int, struct FTW *);
	int (*ofn)(const char *, const struct stat *, int);
	int flags;
	dev_t dev;
	char path[PATH_MAX + 1];
};

static int call(struct walk *w, const struct stat *st, int type, int base, int level)
{
	if (w->ofn)
		return w->ofn(w->path, st, type);
	struct FTW f = { base, level };
	return w->nfn(w->path, st, type, &f);
}

static int walk(struct walk *w, size_t len, int base, int level, const struct anc *up)
{
	struct stat st;
	int type;
	int phys = w->flags & FTW_PHYS;
	/* with FTW_CHDIR we are already in the parent directory */
	const char *name = (w->flags & FTW_CHDIR) && level ? w->path + base : w->path;
	if ((phys ? lstat(name, &st) : stat(name, &st)) == 0) {
		type = S_ISDIR(st.st_mode) ? FTW_D : S_ISLNK(st.st_mode) ? FTW_SL : FTW_F;
	} else if (!phys && lstat(name, &st) == 0 && S_ISLNK(st.st_mode)) {
		type = FTW_SLN;
	} else {
		if (errno == EACCES || errno == ENOENT || errno == ENOTDIR || errno == ELOOP || errno == ENAMETOOLONG) {
			memset(&st, 0, sizeof st);
			type = FTW_NS;
		} else {
			return -1;
		}
	}
	if (level == 0)
		w->dev = st.st_dev;
	else if ((w->flags & FTW_MOUNT) && type != FTW_NS && st.st_dev != w->dev)
		return 0;
	if (type != FTW_D)
		return call(w, &st, type, base, level);

	for (const struct anc *a = up; a; a = a->up)
		if (a->dev == st.st_dev && a->ino == st.st_ino)
			return 0;
	if (!phys) {
		for (size_t i = 0; i < w->nseen; i++)
			if (w->seen[i].dev == st.st_dev && w->seen[i].ino == st.st_ino)
				return 0;
		if (w->nseen == w->cap) {
			size_t cap = w->cap ? 2 * w->cap : 32;
			struct anc *ns = realloc(w->seen, cap * sizeof *ns);
			if (!ns)
				return -1;
			w->seen = ns;
			w->cap = cap;
		}
		w->seen[w->nseen++] = (struct anc){ 0, st.st_dev, st.st_ino };
	}
	struct anc me = { up, st.st_dev, st.st_ino };

	DIR *d = opendir(name);
	if (!d) {
		if (errno == EACCES)
			return call(w, &st, FTW_DNR, base, level);
		return -1;
	}
	int r = 0;
	if (!(w->flags & FTW_DEPTH) && (r = call(w, &st, FTW_D, base, level))) {
		closedir(d);
		return r;
	}
	int here = -1;
	if (w->flags & FTW_CHDIR) {
		here = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (here < 0 || fchdir(dirfd(d)) < 0) {
			if (here >= 0)
				close(here);
			closedir(d);
			return -1;
		}
	}
	size_t l = len;
	if (l && w->path[l - 1] != '/')
		w->path[l++] = '/';
	struct dirent *e;
	while (!r && (e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		size_t nl = strlen(e->d_name);
		if (l + nl >= sizeof w->path) {
			errno = ENAMETOOLONG;
			r = -1;
			break;
		}
		memcpy(w->path + l, e->d_name, nl + 1);
		r = walk(w, l + nl, (int)l, level + 1, &me);
	}
	w->path[len] = 0;
	closedir(d);
	if (here >= 0) {
		int e2 = fchdir(here);
		close(here);
		if (e2 < 0 && !r)
			r = -1;
	}
	if (!r && (w->flags & FTW_DEPTH))
		r = call(w, &st, FTW_DP, base, level);
	return r;
}

static int start(struct walk *w, const char *path)
{
	size_t n = strlen(path);
	if (n > PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(w->path, path, n + 1);
	while (n > 1 && w->path[n - 1] == '/')
		w->path[--n] = 0;
	/* the base is the offset of the last component */
	size_t b = n;
	while (b > 1 && w->path[b - 1] == '/')
		b--;
	while (b && w->path[b - 1] != '/')
		b--;
	w->seen = 0;
	w->nseen = w->cap = 0;
	int r = walk(w, n, (int)b, 0, 0);
	free(w->seen);
	return r;
}

int nftw(const char *path, int (*fn)(const char *, const struct stat *, int, struct FTW *), int fdlimit, int flags)
{
	(void)fdlimit;
	struct walk w;
	w.nfn = fn;
	w.ofn = 0;
	w.flags = flags;
	return start(&w, path);
}

int ftw(const char *path, int (*fn)(const char *, const struct stat *, int), int fdlimit)
{
	(void)fdlimit;
	struct walk w;
	w.nfn = 0;
	w.ofn = fn;
	w.flags = 0;
	return start(&w, path);
}
