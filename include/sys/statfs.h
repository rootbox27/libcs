#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H
#include <features.h>
#include <sys/types.h>
__BEGIN_DECLS
typedef struct { int __val[2]; } fsid_t;
struct statfs {
	long f_type, f_bsize;
	fsblkcnt_t f_blocks, f_bfree, f_bavail;
	fsfilcnt_t f_files, f_ffree;
	fsid_t f_fsid;
	long f_namelen, f_frsize, f_flags, f_spare[4];
};
int statfs(const char *, struct statfs *);
int fstatfs(int, struct statfs *);
__END_DECLS
#endif
