#ifndef _SYS_SHM_H
#define _SYS_SHM_H
#include <features.h>
#include <sys/ipc.h>
#include <time.h>
__BEGIN_DECLS
typedef unsigned long shmatt_t;
struct shmid_ds {
	struct ipc_perm shm_perm;
	size_t shm_segsz;
	time_t shm_atime, shm_dtime, shm_ctime;
	pid_t shm_cpid, shm_lpid;
	shmatt_t shm_nattch;
	unsigned long __unused[2];
};
#define SHM_RDONLY 010000
#define SHM_RND 020000
#define SHM_REMAP 040000
#define SHM_EXEC 0100000
#define SHM_LOCK 11
#define SHM_UNLOCK 12
#define SHM_STAT 13
#define SHM_INFO 14
#define SHMLBA 4096
int shmget(key_t, size_t, int);
void *shmat(int, const void *, int);
int shmdt(const void *);
int shmctl(int, int, struct shmid_ds *);
__END_DECLS
#endif
