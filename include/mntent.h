#ifndef _MNTENT_H
#define _MNTENT_H
#include <features.h>
#include <stdio.h>
__BEGIN_DECLS
#define MOUNTED "/etc/mtab"
#define MNTTAB "/etc/fstab"
#define MNTTYPE_IGNORE "ignore"
#define MNTTYPE_NFS "nfs"
#define MNTTYPE_SWAP "swap"
#define MNTOPT_DEFAULTS "defaults"
#define MNTOPT_RO "ro"
#define MNTOPT_RW "rw"
#define MNTOPT_SUID "suid"
#define MNTOPT_NOSUID "nosuid"
#define MNTOPT_NOAUTO "noauto"
struct mntent {
	char *mnt_fsname, *mnt_dir, *mnt_type, *mnt_opts;
	int mnt_freq, mnt_passno;
};
FILE *setmntent(const char *, const char *);
struct mntent *getmntent(FILE *);
struct mntent *getmntent_r(FILE *, struct mntent *, char *, int);
int addmntent(FILE *, const struct mntent *);
int endmntent(FILE *);
char *hasmntopt(const struct mntent *, const char *);
__END_DECLS
#endif
