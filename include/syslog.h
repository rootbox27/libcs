#ifndef _SYSLOG_H
#define _SYSLOG_H
#include <features.h>
#include <stdarg.h>
__BEGIN_DECLS
#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
#define LOG_KERN (0<<3)
#define LOG_USER (1<<3)
#define LOG_MAIL (2<<3)
#define LOG_DAEMON (3<<3)
#define LOG_AUTH (4<<3)
#define LOG_SYSLOG (5<<3)
#define LOG_LOCAL0 (16<<3)
#define LOG_LOCAL1 (17<<3)
#define LOG_LOCAL7 (23<<3)
#define LOG_PID 0x01
#define LOG_CONS 0x02
#define LOG_NDELAY 0x08
#define LOG_PERROR 0x20
#define LOG_MASK(p) (1 << (p))
#define LOG_UPTO(p) ((1 << ((p)+1)) - 1)
void openlog(const char *, int, int);
void syslog(int, const char *, ...) __printflike(2, 3);
void vsyslog(int, const char *, va_list) __printflike(2, 0);
void closelog(void);
int setlogmask(int);
__END_DECLS
#endif
