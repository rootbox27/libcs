/* fmtmsg: the output layout follows glibc's, field for field. */
#include <fmtmsg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { LABEL = 1, SEVERITY = 2, TEXT = 4, ACTION = 8, TAG = 16, ALL = 31 };

static int msgverb(void)
{
	static const char *const kw[] = { "label", "severity", "text", "action", "tag" };
	const char *s = getenv("MSGVERB");
	if (!s || !*s)
		return ALL;
	int m = 0;
	while (*s) {
		size_t n = strcspn(s, ":");
		int i;
		for (i = 0; i < 5; i++)
			if (strlen(kw[i]) == n && !memcmp(kw[i], s, n))
				break;
		if (i == 5)
			return ALL; /* one bad keyword voids the variable */
		m |= 1 << i;
		s += n + (s[n] == ':');
	}
	return m;
}

static int valid_label(const char *l)
{
	const char *c = strchr(l, ':');
	return c && c - l <= 10 && strlen(c + 1) <= 14;
}

/* build the message into buf; 0 if it does not fit */
static int build(char *buf, size_t n, int want, const char *label, const char *sev, const char *text,
                 const char *action, const char *tag)
{
	int dl = label && (want & LABEL), ds = sev && (want & SEVERITY), dt = text && (want & TEXT);
	int da = action && (want & ACTION), dg = tag && (want & TAG);
	int r = snprintf(buf, n, "%s%s%s%s%s%s%s%s%s%s%s\n",
	                 dl ? label : "", dl && (ds || dt || da || dg) ? ": " : "",
	                 ds ? sev : "", ds && (dt || da || dg) ? ": " : "",
	                 dt ? text : "", dt && (da || dg) ? "\n" : "",
	                 da ? "TO FIX: " : "", da ? action : "", da && dg ? "  " : "",
	                 dg ? tag : "", "");
	return r >= 0 && (size_t)r < n ? r : 0;
}

int fmtmsg(long class, const char *label, int severity, const char *text, const char *action, const char *tag)
{
	static const char *const sevs[] = { 0, "HALT", "ERROR", "WARNING", "INFO" };
	if ((label && !valid_label(label)) || severity < 0 || severity > MM_INFO)
		return MM_NOTOK;
	const char *sev = sevs[severity];
	char buf[4096];
	int ok_print = 1, ok_con = 1;
	if (class & MM_PRINT) {
		int n = build(buf, sizeof buf, msgverb(), label, sev, text, action, tag);
		ok_print = n && write(2, buf, (size_t)n) == n;
	}
	if (class & MM_CONSOLE) {
		int n = build(buf, sizeof buf, ALL, label, sev, text, action, tag);
		int fd = open("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
		ok_con = fd >= 0 && n && write(fd, buf, (size_t)n) == n;
		if (fd >= 0)
			close(fd);
	}
	if (ok_print && ok_con)
		return MM_OK;
	if (!ok_print && !ok_con)
		return MM_NOTOK;
	return ok_print ? MM_NOCON : MM_NOMSG;
}
