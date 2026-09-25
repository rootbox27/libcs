/* Thin wrappers: timerfd, signalfd, inotify, extended attributes,
 * sysinfo, mount, statfs, System V IPC. */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/ipc.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/timerfd.h>
#include <sys/xattr.h>
#include "internal.h"

int timerfd_create(int clk, int flags) { return (int)sys(SYS_timerfd_create, clk, flags); }

int timerfd_settime(int fd, int flags, const struct itimerspec *nv, struct itimerspec *old)
{
	return (int)sys(SYS_timerfd_settime, fd, flags, nv, old);
}

int timerfd_gettime(int fd, struct itimerspec *cur) { return (int)sys(SYS_timerfd_gettime, fd, cur); }

int signalfd(int fd, const sigset_t *mask, int flags)
{
	return (int)sys(SYS_signalfd4, fd, mask, 8, flags);
}

int inotify_init(void) { return (int)sys(SYS_inotify_init1, 0); }
int inotify_init1(int flags) { return (int)sys(SYS_inotify_init1, flags); }
int inotify_add_watch(int fd, const char *path, uint32_t mask) { return (int)sys(SYS_inotify_add_watch, fd, path, mask); }
int inotify_rm_watch(int fd, int wd) { return (int)sys(SYS_inotify_rm_watch, fd, wd); }

ssize_t getxattr(const char *p, const char *n, void *v, size_t s) { return sys(SYS_getxattr, p, n, v, s); }
ssize_t lgetxattr(const char *p, const char *n, void *v, size_t s) { return sys(SYS_lgetxattr, p, n, v, s); }
ssize_t fgetxattr(int fd, const char *n, void *v, size_t s) { return sys(SYS_fgetxattr, fd, n, v, s); }
ssize_t listxattr(const char *p, char *l, size_t s) { return sys(SYS_listxattr, p, l, s); }
ssize_t llistxattr(const char *p, char *l, size_t s) { return sys(SYS_llistxattr, p, l, s); }
ssize_t flistxattr(int fd, char *l, size_t s) { return sys(SYS_flistxattr, fd, l, s); }
int setxattr(const char *p, const char *n, const void *v, size_t s, int f) { return (int)sys(SYS_setxattr, p, n, v, s, f); }
int lsetxattr(const char *p, const char *n, const void *v, size_t s, int f) { return (int)sys(SYS_lsetxattr, p, n, v, s, f); }
int fsetxattr(int fd, const char *n, const void *v, size_t s, int f) { return (int)sys(SYS_fsetxattr, fd, n, v, s, f); }
int removexattr(const char *p, const char *n) { return (int)sys(SYS_removexattr, p, n); }
int lremovexattr(const char *p, const char *n) { return (int)sys(SYS_lremovexattr, p, n); }
int fremovexattr(int fd, const char *n) { return (int)sys(SYS_fremovexattr, fd, n); }

int sysinfo(struct sysinfo *info) { return (int)sys(SYS_sysinfo, info); }
int get_nprocs(void) { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
int get_nprocs_conf(void) { return (int)sysconf(_SC_NPROCESSORS_CONF); }

static long pages(int avail)
{
	struct sysinfo si;
	if (sysinfo(&si) < 0)
		return -1;
	unsigned long long b = (unsigned long long)(avail ? si.freeram : si.totalram) * si.mem_unit;
	return (long)(b / (unsigned long long)sysconf(_SC_PAGESIZE));
}

long get_phys_pages(void) { return pages(0); }
long get_avphys_pages(void) { return pages(1); }

int mount(const char *src, const char *target, const char *type, unsigned long flags, const void *data)
{
	return (int)sys(SYS_mount, src, target, type, flags, data);
}

int umount(const char *target) { return (int)sys(SYS_umount2, target, 0); }
int umount2(const char *target, int flags) { return (int)sys(SYS_umount2, target, flags); }

int statfs(const char *path, struct statfs *buf) { return (int)sys(SYS_statfs, path, buf); }
int fstatfs(int fd, struct statfs *buf) { return (int)sys(SYS_fstatfs, fd, buf); }

/* ---- System V IPC ---- */

key_t ftok(const char *path, int id)
{
	struct stat st;
	if (stat(path, &st) < 0)
		return -1;
	return (key_t)((st.st_ino & 0xffff) | ((st.st_dev & 0xff) << 16) | ((unsigned)(id & 0xff) << 24));
}

int shmget(key_t key, size_t size, int flag)
{
	if (size > PTRDIFF_MAX)
		size = SIZE_MAX;
	return (int)sys(SYS_shmget, key, size, flag);
}

void *shmat(int id, const void *addr, int flag) { return (void *)sys(SYS_shmat, id, addr, flag); }
int shmdt(const void *addr) { return (int)sys(SYS_shmdt, addr); }
int shmctl(int id, int cmd, struct shmid_ds *buf) { return (int)sys(SYS_shmctl, id, cmd, buf); }

int semget(key_t key, int n, int flag)
{
	if (n > USHRT_MAX) {
		errno = EINVAL;
		return -1;
	}
	return (int)sys(SYS_semget, key, n, flag);
}

int semop(int id, struct sembuf *ops, size_t n) { return (int)sys(SYS_semop, id, ops, n); }

int semtimedop(int id, struct sembuf *ops, size_t n, const struct timespec *ts)
{
	return (int)sys(SYS_semtimedop, id, ops, n, ts);
}

union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};

int semctl(int id, int num, int cmd, ...)
{
	union semun arg = { 0 };
	switch (cmd & ~0x100) {
	case SETVAL: case GETALL: case SETALL: case IPC_STAT: case IPC_SET:
	case IPC_INFO: case SEM_INFO: case SEM_STAT: {
		va_list ap;
		va_start(ap, cmd);
		arg = va_arg(ap, union semun);
		va_end(ap);
	}
	}
	return (int)sys(SYS_semctl, id, num, cmd, arg.buf);
}

int msgget(key_t key, int flag) { return (int)sys(SYS_msgget, key, flag); }
int msgsnd(int id, const void *msg, size_t n, int flag) { return (int)sys(SYS_msgsnd, id, msg, n, flag); }
ssize_t msgrcv(int id, void *msg, size_t n, long type, int flag) { return sys(SYS_msgrcv, id, msg, n, type, flag); }
int msgctl(int id, int cmd, struct msqid_ds *buf) { return (int)sys(SYS_msgctl, id, cmd, buf); }
