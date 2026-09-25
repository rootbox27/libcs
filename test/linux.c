/* timerfd, signalfd, inotify, xattr, sysinfo, statfs, System V IPC */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/signalfd.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <sys/timerfd.h>
#include <sys/xattr.h>
#include "harness.h"

_Static_assert(sizeof(struct signalfd_siginfo) == 128, "signalfd_siginfo");
_Static_assert(sizeof(struct statfs) == 120, "statfs");
_Static_assert(sizeof(struct ipc_perm) == 48, "ipc_perm");
_Static_assert(sizeof(struct shmid_ds) == 112, "shmid_ds");

static int unsupported(void) { return errno == ENOSYS || errno == EPERM || errno == EACCES || errno == ENOTSUP; }

int main(void)
{
	int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
	CHECK(fd >= 0);
	struct itimerspec its = { { 0, 0 }, { 0, 5000000 } };
	CHECK(timerfd_settime(fd, 0, &its, 0) == 0);
	uint64_t ticks = 0;
	CHECK(read(fd, &ticks, 8) == 8 && ticks == 1);
	close(fd);

	sigset_t s;
	sigemptyset(&s);
	sigaddset(&s, SIGUSR1);
	sigprocmask(SIG_BLOCK, &s, 0);
	fd = signalfd(-1, &s, SFD_CLOEXEC);
	CHECK(fd >= 0);
	raise(SIGUSR1);
	struct signalfd_siginfo si;
	CHECK(read(fd, &si, sizeof si) == sizeof si && si.ssi_signo == SIGUSR1 && si.ssi_pid == (uint32_t)getpid());
	close(fd);

	char dir[] = "/tmp/citadel-ino-XXXXXX";
	CHECK(mkdtemp(dir) != 0);
	fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	CHECK(fd >= 0);
	int wd = inotify_add_watch(fd, dir, IN_CREATE);
	CHECK(wd >= 0);
	char path[64];
	strcpy(path, dir);
	strcat(path, "/f");
	close(open(path, O_CREAT | O_WRONLY, 0600));
	_Alignas(struct inotify_event) char buf[256];
	ssize_t n = read(fd, buf, sizeof buf);
	struct inotify_event *ev = (void *)buf;
	CHECK(n >= (ssize_t)sizeof *ev && ev->wd == wd && (ev->mask & IN_CREATE) && !strcmp(ev->name, "f"));
	CHECK(inotify_rm_watch(fd, wd) == 0);
	close(fd);

	if (setxattr(path, "user.citadel", "v1", 2, XATTR_CREATE) == 0) {
		char v[8] = { 0 };
		CHECK(getxattr(path, "user.citadel", v, sizeof v) == 2 && !memcmp(v, "v1", 2));
		CHECK(setxattr(path, "user.citadel", "v2", 2, XATTR_CREATE) == -1 && errno == EEXIST);
		CHECK(listxattr(path, buf, sizeof buf) > 0);
		CHECK(removexattr(path, "user.citadel") == 0);
	} else {
		CHECK(unsupported());
	}
	unlink(path);
	rmdir(dir);

	struct sysinfo info;
	CHECK(sysinfo(&info) == 0 && info.totalram > 0 && info.mem_unit > 0);
	CHECK(get_nprocs() >= 1 && get_phys_pages() > 0);
	struct statfs sf;
	CHECK(statfs("/", &sf) == 0 && sf.f_bsize > 0);

	int shm = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
	if (shm >= 0) {
		char *p = shmat(shm, 0, 0);
		CHECK(p != (void *)-1);
		strcpy(p, "shared");
		struct shmid_ds ds;
		CHECK(shmctl(shm, IPC_STAT, &ds) == 0 && ds.shm_segsz == 4096 && ds.shm_nattch == 1);
		CHECK(shmdt(p) == 0 && shmctl(shm, IPC_RMID, 0) == 0);
	} else {
		CHECK(unsupported());
	}
	int sem = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
	if (sem >= 0) {
		CHECK(semctl(sem, 0, SETVAL, 2) == 0 && semctl(sem, 0, GETVAL) == 2);
		struct sembuf op = { 0, -2, IPC_NOWAIT };
		CHECK(semop(sem, &op, 1) == 0 && semctl(sem, 0, GETVAL) == 0);
		CHECK(semop(sem, &op, 1) == -1 && errno == EAGAIN);
		CHECK(semctl(sem, 0, IPC_RMID) == 0);
	} else {
		CHECK(unsupported());
	}
	int mq = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
	if (mq >= 0) {
		struct { long type; char text[16]; } m = { 7, "hello" }, r;
		CHECK(msgsnd(mq, &m, 6, 0) == 0);
		CHECK(msgrcv(mq, &r, sizeof r.text, 7, IPC_NOWAIT) == 6 && !strcmp(r.text, "hello"));
		CHECK(msgctl(mq, IPC_RMID, 0) == 0);
	} else {
		CHECK(unsupported());
	}
	CHECK(ftok("/", 'x') != -1);
	return t_done();
}
