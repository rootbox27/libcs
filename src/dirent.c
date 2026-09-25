/* Directory streams. struct dirent matches the kernel's linux_dirent64,
 * so entries are returned straight from the getdents64 buffer. */
#include "internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct __citadel_dir {
	int fd;
	size_t pos, end;
	long tell;
	volatile int lock;
	_Alignas(8) char buf[4096];
};

DIR *fdopendir(int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
		return 0;
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return 0;
	}
	DIR *d = calloc(1, sizeof *d);
	if (!d)
		return 0;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	d->fd = fd;
	return d;
}

DIR *opendir(const char *name)
{
	int fd = open(name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	DIR *d = calloc(1, sizeof *d);
	if (!d) {
		close(fd);
		return 0;
	}
	d->fd = fd;
	return d;
}

int closedir(DIR *d)
{
	int r = close(d->fd);
	free(d);
	return r;
}

struct dirent *readdir(DIR *d)
{
	LOCK(d->lock);
	if (d->pos >= d->end) {
		long n = __sys(SYS_getdents64, d->fd, d->buf, sizeof d->buf);
		if (n <= 0) {
			if (n < 0 && n != -ENOENT)
				errno = (int)-n;
			UNLOCK(d->lock);
			return 0;
		}
		d->end = (size_t)n;
		d->pos = 0;
	}
	struct dirent *de = (struct dirent *)(d->buf + d->pos);
	d->pos += de->d_reclen;
	d->tell = de->d_off;
	UNLOCK(d->lock);
	return de;
}

int readdir_r(DIR *__restrict d, struct dirent *__restrict buf, struct dirent **__restrict res)
{
	int e = errno;
	errno = 0;
	struct dirent *de = readdir(d);
	int r = errno;
	errno = e;
	if (r)
		return r;
	if (de) {
		size_t l = strnlen(de->d_name, sizeof buf->d_name - 1);
		memcpy(buf, de, offsetof(struct dirent, d_name));
		memcpy(buf->d_name, de->d_name, l);
		buf->d_name[l] = 0;
		*res = buf;
	} else {
		*res = 0;
	}
	return 0;
}

void rewinddir(DIR *d)
{
	LOCK(d->lock);
	lseek(d->fd, 0, SEEK_SET);
	d->pos = d->end = 0;
	d->tell = 0;
	UNLOCK(d->lock);
}

void seekdir(DIR *d, long off)
{
	LOCK(d->lock);
	d->tell = lseek(d->fd, off, SEEK_SET);
	d->pos = d->end = 0;
	UNLOCK(d->lock);
}

long telldir(DIR *d)
{
	return d->tell;
}

int dirfd(DIR *d)
{
	return d->fd;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcoll((*a)->d_name, (*b)->d_name);
}

int scandir(const char *path, struct dirent ***res, int (*sel)(const struct dirent *),
            int (*cmp)(const struct dirent **, const struct dirent **))
{
	DIR *d = opendir(path);
	if (!d)
		return -1;
	struct dirent **list = 0, *de;
	size_t n = 0, cap = 0;
	int e = errno;
	errno = 0;
	while ((de = readdir(d))) {
		if (sel && !sel(de))
			continue;
		if (n == cap) {
			size_t nc = cap ? cap * 2 : 32;
			struct dirent **t = reallocarray(list, nc, sizeof *t);
			if (!t)
				break;
			list = t;
			cap = nc;
		}
		struct dirent *copy = malloc(de->d_reclen);
		if (!copy)
			break;
		memcpy(copy, de, de->d_reclen);
		list[n++] = copy;
	}
	closedir(d);
	if (errno || n > (size_t)0x7fffffff) {
		int err = errno ? errno : ENOMEM;
		while (n)
			free(list[--n]);
		free(list);
		errno = err;
		return -1;
	}
	errno = e;
	if (cmp)
		qsort(list, n, sizeof *list, (int (*)(const void *, const void *))cmp);
	*res = list;
	return (int)n;
}
