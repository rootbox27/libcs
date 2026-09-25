/* syslog: RFC 3164 messages to /dev/log over a datagram socket. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

struct sockaddr_un_ { sa_family_t sun_family; char sun_path[108]; };

static volatile int log_lock;
static int log_fd = -1;
static int log_opt, log_facility = LOG_USER, log_mask = 0xff;
static char log_ident[32];

static void do_open(void)
{
	if (log_fd >= 0)
		return;
	log_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (log_fd < 0)
		return;
	struct sockaddr_un_ a = { AF_UNIX, "/dev/log" };
	if (connect(log_fd, (struct sockaddr *)&a, sizeof a) < 0) {
		close(log_fd);
		log_fd = -1;
	}
}

void openlog(const char *ident, int opt, int facility)
{
	LOCK(log_lock);
	if (ident)
		strlcpy(log_ident, ident, sizeof log_ident);
	else
		log_ident[0] = 0;
	log_opt = opt;
	if (facility)
		log_facility = facility;
	if (opt & LOG_NDELAY)
		do_open();
	UNLOCK(log_lock);
}

void closelog(void)
{
	LOCK(log_lock);
	if (log_fd >= 0)
		close(log_fd);
	log_fd = -1;
	UNLOCK(log_lock);
}

int setlogmask(int mask)
{
	int old = log_mask;
	if (mask)
		log_mask = mask;
	return old;
}

void vsyslog(int pri, const char *fmt, va_list ap)
{
	if (!(log_mask & LOG_MASK(pri & 7)) || (pri & ~0x3ff))
		return;
	int e = errno;
	LOCK(log_lock);
	if (!(pri & 0x3f8))
		pri |= log_facility;
	char buf[1024];
	time_t now = time(0);
	struct tm tm;
	char ts[16];
	localtime_r(&now, &tm);
	strftime(ts, sizeof ts, "%b %e %T", &tm);
	const char *id = log_ident[0] ? log_ident : __libc.progname;
	int hl = snprintf(buf, sizeof buf, "<%d>%s %s", pri, ts, id);
	if (log_opt & LOG_PID)
		hl += snprintf(buf + hl, sizeof buf - (size_t)hl, "[%d]", getpid());
	hl += snprintf(buf + hl, sizeof buf - (size_t)hl, ": ");
	errno = e; /* for %m */
	int ml = vsnprintf(buf + hl, sizeof buf - (size_t)hl, fmt, ap);
	if (ml < 0)
		ml = 0;
	size_t len = (size_t)hl + (size_t)ml;
	if (len >= sizeof buf)
		len = sizeof buf - 1;
	/* drop control characters that could forge extra log lines */
	for (size_t i = (size_t)hl; i < len; i++)
		if (buf[i] == '\n' || buf[i] == '\r')
			buf[i] = ' ';
	do_open();
	if (log_fd < 0 || send(log_fd, buf, len, 0) < 0) {
		if (log_fd >= 0) {
			/* the log daemon may have restarted */
			close(log_fd);
			log_fd = -1;
			do_open();
			if (log_fd >= 0)
				send(log_fd, buf, len, 0);
		}
		if (log_fd < 0 && (log_opt & LOG_CONS)) {
			int c = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
			if (c >= 0) {
				dprintf(c, "%.*s\r\n", (int)(len - (size_t)hl), buf + hl);
				close(c);
			}
		}
	}
	if (log_opt & LOG_PERROR)
		dprintf(2, "%.*s\n", (int)(len - (size_t)hl), buf + hl);
	UNLOCK(log_lock);
	errno = e;
}

void syslog(int pri, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}
