#ifndef _UTMPX_H
#define _UTMPX_H
#include <features.h>
#include <sys/time.h>
#include <sys/types.h>
__BEGIN_DECLS
#define EMPTY 0
#define RUN_LVL 1
#define BOOT_TIME 2
#define NEW_TIME 3
#define OLD_TIME 4
#define INIT_PROCESS 5
#define LOGIN_PROCESS 6
#define USER_PROCESS 7
#define DEAD_PROCESS 8
#define ACCOUNTING 9
struct utmpx {
	short ut_type;
	short __pad0;
	pid_t ut_pid;
	char ut_line[32];
	char ut_id[4];
	char ut_user[32];
	char ut_host[256];
	struct { short __e_termination, __e_exit; } ut_exit;
	int ut_session;
	struct { int tv_sec, tv_usec; } ut_tv;
	unsigned ut_addr_v6[4];
	char __unused[20];
};
void setutxent(void);
void endutxent(void);
struct utmpx *getutxent(void);
struct utmpx *getutxid(const struct utmpx *);
struct utmpx *getutxline(const struct utmpx *);
struct utmpx *pututxline(const struct utmpx *);
__END_DECLS
#endif
