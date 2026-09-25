#ifndef _UTMP_H
#define _UTMP_H
#include <utmpx.h>
__BEGIN_DECLS
#define ut_name ut_user
#define UT_LINESIZE 32
#define UT_NAMESIZE 32
#define UT_HOSTSIZE 256
#define utmp utmpx
#define _PATH_UTMP "/var/run/utmp"
#define _PATH_WTMP "/var/log/wtmp"
#define UTMP_FILE _PATH_UTMP
#define WTMP_FILE _PATH_WTMP
void setutent(void);
void endutent(void);
struct utmp *getutent(void);
struct utmp *getutid(const struct utmp *);
struct utmp *getutline(const struct utmp *);
struct utmp *pututline(const struct utmp *);
int utmpname(const char *);
void updwtmp(const char *, const struct utmp *);
void logwtmp(const char *, const char *, const char *);
int login_tty(int);
__END_DECLS
#endif
