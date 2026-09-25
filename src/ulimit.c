#include <ulimit.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/resource.h>

long ulimit(int cmd, ...)
{
	struct rlimit rl;
	if (getrlimit(RLIMIT_FSIZE, &rl) < 0)
		return -1;
	if (cmd == UL_GETFSIZE)
		return rl.rlim_cur == RLIM_INFINITY ? (long)(RLIM_INFINITY / 512) : (long)(rl.rlim_cur / 512);
	if (cmd == UL_SETFSIZE) {
		va_list ap;
		va_start(ap, cmd);
		long v = va_arg(ap, long);
		va_end(ap);
		if (v < 0) {
			errno = EINVAL;
			return -1;
		}
		rl.rlim_cur = (rlim_t)v * 512;
		if (setrlimit(RLIMIT_FSIZE, &rl) < 0)
			return -1;
		return v;
	}
	errno = EINVAL;
	return -1;
}
