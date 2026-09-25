/* Thin system call wrappers: memory mapping, basic file descriptor I/O
 * and process identity. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <unistd.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	if (off & (off_t)(PAGE_SZ - 1)) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	/* Refuse sizes whose pointer differences would overflow ptrdiff_t. */
	if (len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return (void *)sys(SYS_mmap, addr, len, prot, flags, fd, off);
}

int munmap(void *addr, size_t len) { return (int)sys(SYS_munmap, addr, len); }
int mprotect(void *addr, size_t len, int prot)
{
	/* Accept an unaligned start as POSIX allows, widening to whole pages. */
	uintptr_t s = ROUND_DOWN((uintptr_t)addr, PAGE_SZ);
	uintptr_t e = ROUND_UP((uintptr_t)addr + len, PAGE_SZ);
	return (int)sys(SYS_mprotect, s, e - s, prot);
}
int msync(void *addr, size_t len, int flags) { return (int)sys(SYS_msync, addr, len, flags); }
int madvise(void *addr, size_t len, int advice) { return (int)sys(SYS_madvise, addr, len, advice); }
int mlock(const void *addr, size_t len) { return (int)sys(SYS_mlock, addr, len); }
int munlock(const void *addr, size_t len) { return (int)sys(SYS_munlock, addr, len); }
int mlockall(int flags) { return (int)sys(SYS_mlockall, flags); }
int munlockall(void) { return (int)sys(SYS_munlockall); }

ssize_t read(int fd, void *buf, size_t n) { return sys(SYS_read, fd, buf, n); }
ssize_t write(int fd, const void *buf, size_t n) { return sys(SYS_write, fd, buf, n); }
ssize_t pread(int fd, void *buf, size_t n, off_t off) { return sys(SYS_pread64, fd, buf, n, off); }
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) { return sys(SYS_pwrite64, fd, buf, n, off); }
off_t lseek(int fd, off_t off, int whence) { return sys(SYS_lseek, fd, off, whence); }
int dup(int fd) { return (int)sys(SYS_dup, fd); }
int dup2(int old, int new)
{
	if (old == new) {
		/* dup3 rejects old == new; dup2 must just validate old. */
		long r = __sys(SYS_fcntl, old, 1 /* F_GETFD */);
		return r < 0 ? (int)__syscall_ret((unsigned long)r) : new;
	}
	return (int)sys(SYS_dup3, old, new, 0);
}
int dup3(int old, int new, int flags) { return (int)sys(SYS_dup3, old, new, flags); }
int pipe(int fd[2]) { return (int)sys(SYS_pipe2, fd, 0); }
int pipe2(int fd[2], int flags) { return (int)sys(SYS_pipe2, fd, flags); }
int fsync(int fd) { return (int)sys(SYS_fsync, fd); }
int fdatasync(int fd) { return (int)sys(SYS_fdatasync, fd); }

/* close() never reports EINTR: on Linux the descriptor is released
 * regardless, and retrying could close an unrelated, newly opened fd. */
int close(int fd)
{
	long r = __sys(SYS_close, fd);
	if (r == -EINTR)
		r = 0;
	return (int)__syscall_ret((unsigned long)r);
}

pid_t getpid(void) { return (pid_t)__sys(SYS_getpid); }
pid_t getppid(void) { return (pid_t)__sys(SYS_getppid); }
pid_t gettid(void) { return (pid_t)__sys(SYS_gettid); }
uid_t getuid(void) { return (uid_t)__sys(SYS_getuid); }
uid_t geteuid(void) { return (uid_t)__sys(SYS_geteuid); }
gid_t getgid(void) { return (gid_t)__sys(SYS_getgid); }
gid_t getegid(void) { return (gid_t)__sys(SYS_getegid); }
int getpagesize(void) { return (int)PAGE_SZ; }

int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return (int)sys(SYS_openat, AT_FDCWD, path, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return (int)sys(SYS_openat, dirfd, path, flags, mode);
}

int fcntl(int fd, int cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);
	unsigned long arg = va_arg(ap, unsigned long);
	va_end(ap);
	return (int)sys(SYS_fcntl, fd, cmd, arg);
}

int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	va_start(ap, req);
	void *arg = va_arg(ap, void *);
	va_end(ap);
	return (int)sys(SYS_ioctl, fd, req, arg);
}

int isatty(int fd)
{
	unsigned char t[64]; /* struct termios */
	long r = __sys(SYS_ioctl, fd, TCGETS, t);
	if (r < 0) {
		errno = r == -EBADF ? EBADF : ENOTTY;
		return 0;
	}
	return 1;
}

int unlink(const char *path) { return (int)sys(SYS_unlinkat, AT_FDCWD, path, 0); }
int unlinkat(int dirfd, const char *path, int flags) { return (int)sys(SYS_unlinkat, dirfd, path, flags); }
int rmdir(const char *path) { return (int)sys(SYS_unlinkat, AT_FDCWD, path, AT_REMOVEDIR); }
int rename(const char *old, const char *new) { return (int)sys(SYS_renameat, AT_FDCWD, old, AT_FDCWD, new); }
int renameat(int od, const char *old, int nd, const char *new) { return (int)sys(SYS_renameat, od, old, nd, new); }

int remove(const char *path)
{
	long r = __sys(SYS_unlinkat, AT_FDCWD, path, 0);
	if (r == -EISDIR)
		r = __sys(SYS_unlinkat, AT_FDCWD, path, AT_REMOVEDIR);
	return (int)__syscall_ret((unsigned long)r);
}

ssize_t getrandom(void *buf, size_t n, unsigned flags) { return sys(SYS_getrandom, buf, n, flags); }

/* Kernel randomness for internal use; there is no safe fallback, so a
 * failure is fatal rather than silently weak. */
hidden void __secure_random(void *buf, size_t len)
{
	unsigned char *p = buf;
	while (len) {
		long r = __sys(SYS_getrandom, p, len, 0);
		if (r == -EINTR)
			continue;
		if (r <= 0)
			__fatal("getrandom failed");
		p += r;
		len -= (size_t)r;
	}
}

static int cwd_into(char *buf, size_t size)
{
	long r = sys(SYS_getcwd, buf, size);
	if (r < 0)
		return -1;
	/* An unreachable directory comes back as "(unreachable)/..."; never
	 * hand out something that is not an absolute path. */
	if (r == 0 || buf[0] != '/') {
		errno = ENOENT;
		return -1;
	}
	return 0;
}

char *getcwd(char *buf, size_t size)
{
	if (buf) {
		if (!size) {
			errno = EINVAL;
			return 0;
		}
		return cwd_into(buf, size) ? 0 : buf;
	}
	/* GNU extension: allocate the result. */
	char tmp[PATH_MAX];
	if (cwd_into(tmp, sizeof tmp))
		return 0;
	return strdup(tmp);
}

ssize_t readlinkat(int dirfd, const char *__restrict path, char *__restrict buf, size_t n)
{
	char dummy[1];
	if (!n) {
		/* The kernel rejects a zero size; POSIX allows it. */
		buf = dummy;
		n = 1;
		long r = sys(SYS_readlinkat, dirfd, path, buf, n);
		return r < 0 ? r : 0;
	}
	return sys(SYS_readlinkat, dirfd, path, buf, n);
}

ssize_t readlink(const char *__restrict path, char *__restrict buf, size_t n)
{
	return readlinkat(AT_FDCWD, path, buf, n);
}

/* ---- fortify entry points ---- */

ssize_t __read_chk(int fd, void *buf, size_t n, size_t buflen)
{
	if (n > buflen)
		__chk_fail();
	return read(fd, buf, n);
}

ssize_t __pread_chk(int fd, void *buf, size_t n, off_t off, size_t buflen)
{
	if (n > buflen)
		__chk_fail();
	return pread(fd, buf, n, off);
}

char *__getcwd_chk(char *buf, size_t n, size_t buflen)
{
	if (buf && n > buflen)
		__chk_fail();
	return getcwd(buf, n);
}

ssize_t __readlink_chk(const char *path, char *buf, size_t n, size_t buflen)
{
	if (n > buflen)
		__chk_fail();
	return readlink(path, buf, n);
}
