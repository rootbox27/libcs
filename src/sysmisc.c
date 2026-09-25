/* Assorted system interfaces: resource limits, scheduling, I/O
 * multiplexing, file system info, memory mapping extras, system
 * configuration and terminal process groups. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* ---- resources ---- */

int prlimit(pid_t pid, int res, const struct rlimit *n, struct rlimit *o)
{
	return (int)sys(SYS_prlimit64, pid, res, n, o);
}
int getrlimit(int res, struct rlimit *r) { return prlimit(0, res, 0, r); }
int setrlimit(int res, const struct rlimit *r) { return prlimit(0, res, r, 0); }
int getrusage(int who, struct rusage *ru) { return (int)sys(SYS_getrusage, who, ru); }
int getpriority(int which, id_t who)
{
	long r = __sys(SYS_getpriority, which, who);
	if (r < 0)
		return (int)__syscall_ret((unsigned long)r);
	return 20 - (int)r; /* the kernel reports 20 - nice */
}
int setpriority(int which, id_t who, int prio) { return (int)sys(SYS_setpriority, which, who, prio); }

/* ---- scheduling ---- */

int sched_yield(void) { return (int)__sys(SYS_sched_yield); }
int sched_getcpu(void)
{
	unsigned cpu;
	long r = __sys(SYS_getcpu, &cpu, 0, 0);
	return r ? (int)__syscall_ret((unsigned long)r) : (int)cpu;
}
int sched_getaffinity(pid_t pid, size_t n, cpu_set_t *set)
{
	long r = __sys(SYS_sched_getaffinity, pid, n, set);
	if (r < 0)
		return (int)__syscall_ret((unsigned long)r);
	/* the kernel fills only r bytes */
	if ((size_t)r < n)
		memset((char *)set + r, 0, n - (size_t)r);
	return 0;
}
int sched_setaffinity(pid_t pid, size_t n, const cpu_set_t *set) { return (int)sys(SYS_sched_setaffinity, pid, n, set); }
int sched_get_priority_max(int p) { return (int)sys(SYS_sched_get_priority_max, p); }
int sched_get_priority_min(int p) { return (int)sys(SYS_sched_get_priority_min, p); }
int sched_getscheduler(pid_t pid) { return (int)sys(SYS_sched_getscheduler, pid); }
int sched_setscheduler(pid_t pid, int pol, const struct sched_param *p) { return (int)sys(SYS_sched_setscheduler, pid, pol, p); }
int sched_getparam(pid_t pid, struct sched_param *p) { return (int)sys(SYS_sched_getparam, pid, p); }
int sched_setparam(pid_t pid, const struct sched_param *p) { return (int)sys(SYS_sched_setparam, pid, p); }
int unshare(int flags) { return (int)sys(SYS_unshare, flags); }
int __sched_cpucount(size_t n, const cpu_set_t *set)
{
	int c = 0;
	for (size_t i = 0; i < n / sizeof(long); i++)
		c += __builtin_popcountl(set->__bits[i]);
	return c;
}

/* ---- multiplexing ---- */

void __fd_check(int fd)
{
	if (fd < 0 || fd >= FD_SETSIZE)
		__fatal("fd_set index out of range");
}

int poll(struct pollfd *fds, nfds_t n, int timeout)
{
	struct timespec ts, *tp = 0;
	if (timeout >= 0) {
		ts.tv_sec = timeout / 1000;
		ts.tv_nsec = (timeout % 1000) * 1000000L;
		tp = &ts;
	}
	return (int)sys(SYS_ppoll, fds, n, tp, 0, 8);
}

int ppoll(struct pollfd *fds, nfds_t n, const struct timespec *ts, const sigset_t *mask)
{
	return (int)sys(SYS_ppoll, fds, n, ts, mask, 8);
}

int pselect(int n, fd_set *__restrict r, fd_set *__restrict w, fd_set *__restrict e,
            const struct timespec *__restrict ts, const sigset_t *__restrict mask)
{
	if (n < 0 || n > FD_SETSIZE) {
		errno = EINVAL;
		return -1;
	}
	struct { const sigset_t *set; size_t len; } data = { mask, 8 };
	return (int)sys(SYS_pselect6, n, r, w, e, ts, mask ? &data : 0);
}

int select(int n, fd_set *__restrict r, fd_set *__restrict w, fd_set *__restrict e, struct timeval *__restrict tv)
{
	if (n < 0 || n > FD_SETSIZE) {
		errno = EINVAL;
		return -1;
	}
	if (tv && (tv->tv_sec < 0 || (unsigned long)tv->tv_usec >= 1000000)) {
		errno = EINVAL;
		return -1;
	}
	/* select(2) itself updates the timeval with the time left */
	return (int)sys(SYS_select, n, r, w, e, tv);
}

int epoll_create1(int flags) { return (int)sys(SYS_epoll_create1, flags); }
int epoll_create(int size)
{
	if (size <= 0) {
		errno = EINVAL;
		return -1;
	}
	return epoll_create1(0);
}
int epoll_ctl(int ep, int op, int fd, struct epoll_event *ev) { return (int)sys(SYS_epoll_ctl, ep, op, fd, ev); }
int epoll_pwait(int ep, struct epoll_event *ev, int n, int timeout, const sigset_t *mask)
{
	return (int)sys(SYS_epoll_pwait, ep, ev, n, timeout, mask, 8);
}
int epoll_wait(int ep, struct epoll_event *ev, int n, int timeout) { return epoll_pwait(ep, ev, n, timeout, 0); }

int eventfd(unsigned init, int flags) { return (int)sys(SYS_eventfd2, init, flags); }
int eventfd_read(int fd, eventfd_t *v) { return read(fd, v, sizeof *v) == sizeof *v ? 0 : -1; }
int eventfd_write(int fd, eventfd_t v) { return write(fd, &v, sizeof v) == sizeof v ? 0 : -1; }

ssize_t readv(int fd, const struct iovec *iov, int n) { return sys(SYS_readv, fd, iov, n); }
ssize_t writev(int fd, const struct iovec *iov, int n) { return sys(SYS_writev, fd, iov, n); }
ssize_t preadv(int fd, const struct iovec *iov, int n, off_t off) { return sys(SYS_preadv, fd, iov, n, off, 0); }
ssize_t pwritev(int fd, const struct iovec *iov, int n, off_t off) { return sys(SYS_pwritev, fd, iov, n, off, 0); }
ssize_t sendfile(int out, int in, off_t *off, size_t n) { return sys(SYS_sendfile, out, in, off, n); }

/* ---- file systems and files ---- */

struct kstatfs {
	long f_type, f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree;
	int f_fsid[2];
	long f_namelen, f_frsize, f_flags, f_spare[4];
};

static void to_statvfs(const struct kstatfs *k, struct statvfs *v)
{
	memset(v, 0, sizeof *v);
	v->f_bsize = (unsigned long)k->f_bsize;
	v->f_frsize = (unsigned long)(k->f_frsize ? k->f_frsize : k->f_bsize);
	v->f_blocks = (fsblkcnt_t)k->f_blocks;
	v->f_bfree = (fsblkcnt_t)k->f_bfree;
	v->f_bavail = (fsblkcnt_t)k->f_bavail;
	v->f_files = (fsfilcnt_t)k->f_files;
	v->f_ffree = (fsfilcnt_t)k->f_ffree;
	v->f_favail = (fsfilcnt_t)k->f_ffree;
	v->f_fsid = (unsigned)k->f_fsid[0];
	v->f_flag = (unsigned long)k->f_flags & ~0x20UL; /* drop ST_VALID */
	v->f_namemax = (unsigned long)k->f_namelen;
}

int statvfs(const char *__restrict path, struct statvfs *__restrict v)
{
	struct kstatfs k;
	if (sys(SYS_statfs, path, &k) < 0)
		return -1;
	to_statvfs(&k, v);
	return 0;
}

int fstatvfs(int fd, struct statvfs *v)
{
	struct kstatfs k;
	if (sys(SYS_fstatfs, fd, &k) < 0)
		return -1;
	to_statvfs(&k, v);
	return 0;
}

int uname(struct utsname *u) { return (int)sys(SYS_uname, u); }
int flock(int fd, int op) { return (int)sys(SYS_flock, fd, op); }
int chroot(const char *path) { return (int)sys(SYS_chroot, path); }
int creat(const char *path, mode_t mode) { return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode); }
int posix_fadvise(int fd, off_t off, off_t len, int advice) { return (int)-__sys(SYS_fadvise64, fd, off, len, advice); }
int posix_fallocate(int fd, off_t off, off_t len) { return (int)-__sys(SYS_fallocate, fd, 0, off, len); }

int prctl(int op, ...)
{
	va_list ap;
	va_start(ap, op);
	unsigned long a[4];
	for (int i = 0; i < 4; i++)
		a[i] = va_arg(ap, unsigned long);
	va_end(ap);
	return (int)sys(SYS_prctl, op, a[0], a[1], a[2], a[3]);
}

int lockf(int fd, int op, off_t len)
{
	struct flock l = { .l_type = F_WRLCK, .l_whence = SEEK_CUR, .l_start = 0, .l_len = len };
	switch (op) {
	case F_TEST:
		l.l_type = F_RDLCK;
		if (fcntl(fd, F_GETLK, &l) < 0)
			return -1;
		if (l.l_type == F_UNLCK || l.l_pid == getpid())
			return 0;
		errno = EACCES;
		return -1;
	case F_ULOCK:
		l.l_type = F_UNLCK;
		/* fall through */
	case F_TLOCK:
		return fcntl(fd, F_SETLK, &l);
	case F_LOCK:
		return fcntl(fd, F_SETLKW, &l);
	}
	errno = EINVAL;
	return -1;
}

/* ---- memory ---- */

void *mremap(void *old, size_t olen, size_t nlen, int flags, ...)
{
	void *naddr = 0;
	if (flags & MREMAP_FIXED) {
		va_list ap;
		va_start(ap, flags);
		naddr = va_arg(ap, void *);
		va_end(ap);
	}
	if (nlen >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	return (void *)sys(SYS_mremap, old, olen, nlen, flags, naddr);
}

int posix_madvise(void *addr, size_t len, int advice)
{
	if (advice == MADV_DONTNEED)
		return 0; /* POSIX_MADV_DONTNEED must not discard data */
	return (int)-__sys(SYS_madvise, addr, len, advice);
}

int memfd_create(const char *name, unsigned flags) { return (int)sys(SYS_memfd_create, name, flags); }

static int shm_path(const char *name, char *buf)
{
	while (*name == '/')
		name++;
	size_t l = strnlen(name, NAME_MAX + 1);
	if (!l || l > NAME_MAX || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) {
		errno = l > NAME_MAX ? ENAMETOOLONG : EINVAL;
		return -1;
	}
	memcpy(buf, "/dev/shm/", 9);
	memcpy(buf + 9, name, l + 1);
	return 0;
}

int shm_open(const char *name, int flags, mode_t mode)
{
	char buf[NAME_MAX + 10];
	if (shm_path(name, buf))
		return -1;
	return open(buf, flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, mode);
}

int shm_unlink(const char *name)
{
	char buf[NAME_MAX + 10];
	if (shm_path(name, buf))
		return -1;
	return unlink(buf);
}

/* ---- configuration ---- */

long sysconf(int name)
{
	struct rlimit r;
	switch (name) {
	case _SC_ARG_MAX: return 131072 * 16;
	case _SC_CHILD_MAX:
		return getrlimit(RLIMIT_NPROC, &r) || r.rlim_cur == RLIM_INFINITY ? -1 : (long)r.rlim_cur;
	case _SC_CLK_TCK: return 100;
	case _SC_NGROUPS_MAX: return 65536;
	case _SC_OPEN_MAX:
		return getrlimit(RLIMIT_NOFILE, &r) || r.rlim_cur == RLIM_INFINITY ? -1 : (long)r.rlim_cur;
	case _SC_PAGESIZE: return (long)PAGE_SZ;
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN: {
		cpu_set_t s;
		if (sched_getaffinity(0, sizeof s, &s))
			return 1;
		return CPU_COUNT(&s);
	}
	case _SC_PHYS_PAGES: {
		struct { long uptime; unsigned long loads[3], totalram, freeram, sharedram, bufferram, totalswap, freeswap;
		         unsigned short procs, pad; unsigned long totalhigh, freehigh; unsigned mem_unit; char pad2[8]; } si;
		if (__sys(SYS_sysinfo, &si))
			return -1;
		return (long)(si.totalram * (si.mem_unit ? si.mem_unit : 1) / PAGE_SZ);
	}
	case _SC_LINE_MAX: return 2048;
	case _SC_HOST_NAME_MAX: return 64;
	case _SC_GETPW_R_SIZE_MAX:
	case _SC_GETGR_R_SIZE_MAX: return 4096;
	case _SC_THREAD_STACK_MIN: return 16384;
	case _SC_IOV_MAX: return 1024;
	}
	errno = EINVAL;
	return -1;
}

static long pconf(int name)
{
	switch (name) {
	case _PC_PATH_MAX: return PATH_MAX;
	case _PC_NAME_MAX: return NAME_MAX;
	case _PC_PIPE_BUF: return 4096;
	}
	errno = EINVAL;
	return -1;
}
long pathconf(const char *path, int name) { return pconf(name); }
long fpathconf(int fd, int name) { return pconf(name); }
int getdtablesize(void)
{
	struct rlimit r;
	return getrlimit(RLIMIT_NOFILE, &r) || r.rlim_cur > INT_MAX ? INT_MAX : (int)r.rlim_cur;
}

int gethostname(char *name, size_t len)
{
	struct utsname u;
	if (uname(&u))
		return -1;
	size_t l = strnlen(u.nodename, sizeof u.nodename);
	if (l >= len) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(name, u.nodename, l + 1);
	return 0;
}
int sethostname(const char *name, size_t len) { return (int)sys(SYS_sethostname, name, len); }

int getentropy(void *buf, size_t len)
{
	if (len > 256) {
		errno = EIO;
		return -1;
	}
	unsigned char *p = buf;
	while (len) {
		long r = __sys(SYS_getrandom, p, len, 0);
		if (r == -EINTR)
			continue;
		if (r < 0)
			return (int)__syscall_ret((unsigned long)r);
		p += r;
		len -= (size_t)r;
	}
	return 0;
}

long syscall(long n, ...)
{
	va_list ap;
	va_start(ap, n);
	long a = va_arg(ap, long), b = va_arg(ap, long), c = va_arg(ap, long);
	long d = va_arg(ap, long), e = va_arg(ap, long), f = va_arg(ap, long);
	va_end(ap);
	return __syscall_ret((unsigned long)__syscall6(n, a, b, c, d, e, f));
}

/* The program break is not used by this library; only sbrk(0) is
 * answered, since growing the break behind the allocator is unsafe. */
int brk(void *end)
{
	errno = ENOMEM;
	return -1;
}
void *sbrk(long inc)
{
	if (inc) {
		errno = ENOMEM;
		return (void *)-1;
	}
	return (void *)__sys(SYS_brk, 0);
}

int getloadavg(double *a, int n)
{
	struct { long uptime; unsigned long loads[3]; char rest[128]; } si;
	if (n <= 0)
		return n ? -1 : 0;
	if (__sys(SYS_sysinfo, &si))
		return -1;
	if (n > 3)
		n = 3;
	for (int i = 0; i < n; i++)
		a[i] = si.loads[i] / 65536.0;
	return n;
}

void *recallocarray(void *p, size_t oldn, size_t n, size_t size)
{
	size_t oldsz, newsz;
	if (!p)
		return calloc(n, size);
	if (__builtin_mul_overflow(n, size, &newsz) || __builtin_mul_overflow(oldn, size, &oldsz)) {
		errno = ENOMEM;
		return 0;
	}
	/* never realloc in place: the old contents are cleared */
	void *q = malloc(newsz ? newsz : 1);
	if (!q)
		return 0;
	if (newsz > oldsz) {
		memcpy(q, p, oldsz);
		memset((char *)q + oldsz, 0, newsz - oldsz);
	} else {
		memcpy(q, p, newsz);
	}
	freezero(p, oldsz);
	return q;
}

/* ---- terminals ---- */

pid_t tcgetpgrp(int fd)
{
	int pg;
	if (ioctl(fd, TIOCGPGRP, &pg) < 0)
		return -1;
	return pg;
}

int tcsetpgrp(int fd, pid_t pg)
{
	int p = pg;
	return ioctl(fd, TIOCSPGRP, &p);
}

int ttyname_r(int fd, char *buf, size_t len)
{
	if (!isatty(fd))
		return errno;
	char proc[32];
	snprintf(proc, sizeof proc, "/proc/self/fd/%d", fd);
	ssize_t l = readlink(proc, buf, len);
	if (l < 0)
		return errno;
	if ((size_t)l >= len)
		return ERANGE;
	buf[l] = 0;
	/* make sure the name still refers to this terminal */
	struct stat a, b;
	if (stat(buf, &a) || fstat(fd, &b) || a.st_rdev != b.st_rdev || a.st_ino != b.st_ino)
		return ENODEV;
	return 0;
}

char *ttyname(int fd)
{
	static char buf[PATH_MAX];
	int r = ttyname_r(fd, buf, sizeof buf);
	if (r) {
		errno = r;
		return 0;
	}
	return buf;
}

char *ctermid(char *s)
{
	return s ? strcpy(s, "/dev/tty") : (char *)"/dev/tty";
}
