/* File system system call wrappers. Everything path-based goes through
 * the *at system calls. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int fstatat(int dirfd, const char *__restrict path, struct stat *__restrict st, int flags)
{
	return (int)sys(SYS_newfstatat, dirfd, path, st, flags);
}
int stat(const char *__restrict path, struct stat *__restrict st) { return fstatat(AT_FDCWD, path, st, 0); }
int lstat(const char *__restrict path, struct stat *__restrict st) { return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW); }
int fstat(int fd, struct stat *st)
{
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	return (int)sys(SYS_fstat, fd, st);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	if (flags & ~AT_SYMLINK_NOFOLLOW) {
		errno = EINVAL;
		return -1;
	}
	if (flags) {
		/* Linux cannot chmod a symlink itself; report that honestly
		 * rather than following it. */
		struct stat st;
		if (fstatat(dirfd, path, &st, AT_SYMLINK_NOFOLLOW))
			return -1;
		if (S_ISLNK(st.st_mode)) {
			errno = EOPNOTSUPP;
			return -1;
		}
	}
	return (int)sys(SYS_fchmodat, dirfd, path, mode);
}
int chmod(const char *path, mode_t mode) { return (int)sys(SYS_fchmodat, AT_FDCWD, path, mode); }
int fchmod(int fd, mode_t mode) { return (int)sys(SYS_fchmod, fd, mode); }
mode_t umask(mode_t mask) { return (mode_t)__sys(SYS_umask, mask); }
int mkdirat(int dirfd, const char *path, mode_t mode) { return (int)sys(SYS_mkdirat, dirfd, path, mode); }
int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }
int mknod(const char *path, mode_t mode, dev_t dev) { return (int)sys(SYS_mknodat, AT_FDCWD, path, mode, dev); }
int mkfifo(const char *path, mode_t mode) { return mknod(path, (mode & ~S_IFMT) | S_IFIFO, 0); }
int utimensat(int dirfd, const char *path, const struct timespec ts[2], int flags)
{
	return (int)sys(SYS_utimensat, dirfd, path, ts, flags);
}
int futimens(int fd, const struct timespec ts[2]) { return utimensat(fd, 0, ts, 0); }

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	if (!flags)
		return (int)sys(SYS_faccessat, dirfd, path, mode);
	long r = __sys(SYS_faccessat2, dirfd, path, mode, flags);
	return (int)__syscall_ret((unsigned long)r);
}
int access(const char *path, int mode) { return faccessat(AT_FDCWD, path, mode, 0); }
int chdir(const char *path) { return (int)sys(SYS_chdir, path); }
int fchdir(int fd) { return (int)sys(SYS_fchdir, fd); }
int fchownat(int dirfd, const char *path, uid_t u, gid_t g, int flags)
{
	return (int)sys(SYS_fchownat, dirfd, path, u, g, flags);
}
int chown(const char *path, uid_t u, gid_t g) { return fchownat(AT_FDCWD, path, u, g, 0); }
int lchown(const char *path, uid_t u, gid_t g) { return fchownat(AT_FDCWD, path, u, g, AT_SYMLINK_NOFOLLOW); }
int fchown(int fd, uid_t u, gid_t g) { return (int)sys(SYS_fchown, fd, u, g); }
int linkat(int od, const char *old, int nd, const char *new, int flags)
{
	return (int)sys(SYS_linkat, od, old, nd, new, flags);
}
int link(const char *old, const char *new) { return linkat(AT_FDCWD, old, AT_FDCWD, new, 0); }
int symlinkat(const char *target, int dirfd, const char *path) { return (int)sys(SYS_symlinkat, target, dirfd, path); }
int symlink(const char *target, const char *path) { return symlinkat(target, AT_FDCWD, path); }
int truncate(const char *path, off_t len) { return (int)sys(SYS_truncate, path, len); }
int ftruncate(int fd, off_t len) { return (int)sys(SYS_ftruncate, fd, len); }
void sync(void) { __sys(SYS_sync); }
