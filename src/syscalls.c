/* Thin system call wrappers: memory mapping, basic file descriptor I/O
 * and process identity. */
#include "internal.h"
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
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
