#ifndef _GRP_H
#define _GRP_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
struct group { char *gr_name, *gr_passwd; gid_t gr_gid; char **gr_mem; };
struct group *getgrnam(const char *);
struct group *getgrgid(gid_t);
int getgrnam_r(const char *, struct group *, char *, size_t, struct group **);
int getgrgid_r(gid_t, struct group *, char *, size_t, struct group **);
int initgroups(const char *, gid_t);
__END_DECLS
#endif
