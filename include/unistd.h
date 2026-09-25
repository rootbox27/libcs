#ifndef _UNISTD_H
#define _UNISTD_H
#include <features.h>
#include <bits/alltypes.h>
#define __need_NULL
#include <stddef.h>
__BEGIN_DECLS
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
#define SEEK_DATA 3
#define SEEK_HOLE 4
#define _POSIX_VERSION 200809L
#define _POSIX_THREADS 200809L
#define _POSIX_TIMERS 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L
#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_NGROUPS_MAX 3
#define _SC_OPEN_MAX 4
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE 30
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES 85
#define _SC_LINE_MAX 43
#define _SC_HOST_NAME_MAX 180
#define _SC_GETPW_R_SIZE_MAX 70
#define _SC_GETGR_R_SIZE_MAX 69
#define _SC_THREAD_STACK_MIN 75
#define _SC_IOV_MAX 60
#define _PC_PATH_MAX 4
#define _PC_NAME_MAX 3
#define _PC_PIPE_BUF 5

ssize_t read(int, void *, size_t) __wur;
ssize_t write(int, const void *, size_t);
ssize_t pread(int, void *, size_t, off_t) __wur;
ssize_t pwrite(int, const void *, size_t, off_t);
int close(int);
off_t lseek(int, off_t, int);
int dup(int);
int dup2(int, int);
int dup3(int, int, int);
int pipe(int[2]);
int pipe2(int[2], int);
int fsync(int);
int fdatasync(int);
void sync(void);
int ftruncate(int, off_t);
int truncate(const char *, off_t);
int access(const char *, int);
int faccessat(int, const char *, int, int);
int chdir(const char *);
int fchdir(int);
char *getcwd(char *, size_t);
int chown(const char *, uid_t, gid_t);
int fchown(int, uid_t, gid_t);
int fchownat(int, const char *, uid_t, gid_t, int);
int lchown(const char *, uid_t, gid_t);
int link(const char *, const char *);
int linkat(int, const char *, int, const char *, int);
int symlink(const char *, const char *);
int symlinkat(const char *, int, const char *);
ssize_t readlink(const char *__restrict, char *__restrict, size_t);
ssize_t readlinkat(int, const char *__restrict, char *__restrict, size_t);
int unlink(const char *);
int unlinkat(int, const char *, int);
int rmdir(const char *);
int isatty(int);
char *ttyname(int);
int ttyname_r(int, char *, size_t);
pid_t getpid(void);
pid_t getppid(void);
pid_t gettid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t);
int setpgid(pid_t, pid_t);
pid_t setsid(void);
pid_t getsid(pid_t);
pid_t tcgetpgrp(int);
int tcsetpgrp(int, pid_t);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t);
int seteuid(uid_t);
int setgid(gid_t);
int setegid(gid_t);
int setreuid(uid_t, uid_t);
int setregid(gid_t, gid_t);
int setresuid(uid_t, uid_t, uid_t);
int setresgid(gid_t, gid_t, gid_t);
int getresuid(uid_t *, uid_t *, uid_t *);
int getresgid(gid_t *, gid_t *, gid_t *);
int getgroups(int, gid_t[]);
int setgroups(size_t, const gid_t *);
pid_t fork(void);
pid_t vfork(void);
int execve(const char *, char *const[], char *const[]);
int execv(const char *, char *const[]);
int execvp(const char *, char *const[]);
int execvpe(const char *, char *const[], char *const[]);
int execl(const char *, const char *, ...);
int execlp(const char *, const char *, ...);
int execle(const char *, const char *, ...);
int fexecve(int, char *const[], char *const[]);
__noreturn void _exit(int);
unsigned sleep(unsigned);
int usleep(useconds_t);
unsigned alarm(unsigned);
int pause(void);
long sysconf(int);
long pathconf(const char *, int);
long fpathconf(int, int);
int getpagesize(void);
int gethostname(char *, size_t);
int sethostname(const char *, size_t);
int getdtablesize(void);
int nice(int);
int chroot(const char *);
char *getlogin(void);
int getlogin_r(char *, size_t);
int getentropy(void *, size_t);
int issetugid(void);
long syscall(long, ...);
int brk(void *);
void *sbrk(long);
int lockf(int, int, off_t);
#define F_ULOCK 0
#define F_LOCK 1
#define F_TLOCK 2
#define F_TEST 3
int getopt(int, char *const[], const char *);
extern char *optarg;
extern int optind, opterr, optopt;
extern char **environ;
char *crypt(const char *, const char *);
__END_DECLS
#ifdef __CITADEL_FORTIFY
#include <bits/fortify_unistd.h>
#endif
#endif
