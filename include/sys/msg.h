#ifndef _SYS_MSG_H
#define _SYS_MSG_H
#include <features.h>
#include <sys/ipc.h>
#include <time.h>
__BEGIN_DECLS
typedef unsigned long msgqnum_t, msglen_t;
struct msqid_ds {
	struct ipc_perm msg_perm;
	time_t msg_stime, msg_rtime, msg_ctime;
	unsigned long __msg_cbytes;
	msgqnum_t msg_qnum;
	msglen_t msg_qbytes;
	pid_t msg_lspid, msg_lrpid;
	unsigned long __unused[2];
};
#define MSG_NOERROR 010000
#define MSG_EXCEPT 020000
#define MSG_COPY 040000
#define MSG_STAT 11
#define MSG_INFO 12
int msgget(key_t, int);
int msgsnd(int, const void *, size_t, int);
ssize_t msgrcv(int, void *, size_t, long, int);
int msgctl(int, int, struct msqid_ds *);
__END_DECLS
#endif
