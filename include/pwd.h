#ifndef _PWD_H
#define _PWD_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
struct passwd { char *pw_name, *pw_passwd; uid_t pw_uid; gid_t pw_gid; char *pw_gecos, *pw_dir, *pw_shell; };
struct passwd *getpwnam(const char *);
struct passwd *getpwuid(uid_t);
int getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);
int getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);
void setpwent(void);
void endpwent(void);
struct passwd *getpwent(void);
__END_DECLS
#endif
