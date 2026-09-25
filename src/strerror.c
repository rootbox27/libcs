/* Error and signal descriptions. The errno table is generated from the
 * constants in include/errno.h. */
#include "internal.h"
#include <errno.h>
#include <signal.h>
#include <string.h>

static const struct {
	const char *name, *msg;
} errtab[] = {
	[EPERM] = { "EPERM", "Operation not permitted" },
	[ENOENT] = { "ENOENT", "No such file or directory" },
	[ESRCH] = { "ESRCH", "No such process" },
	[EINTR] = { "EINTR", "Interrupted system call" },
	[EIO] = { "EIO", "Input/output error" },
	[ENXIO] = { "ENXIO", "No such device or address" },
	[E2BIG] = { "E2BIG", "Argument list too long" },
	[ENOEXEC] = { "ENOEXEC", "Exec format error" },
	[EBADF] = { "EBADF", "Bad file descriptor" },
	[ECHILD] = { "ECHILD", "No child processes" },
	[EAGAIN] = { "EAGAIN", "Resource temporarily unavailable" },
	[ENOMEM] = { "ENOMEM", "Cannot allocate memory" },
	[EACCES] = { "EACCES", "Permission denied" },
	[EFAULT] = { "EFAULT", "Bad address" },
	[ENOTBLK] = { "ENOTBLK", "Block device required" },
	[EBUSY] = { "EBUSY", "Device or resource busy" },
	[EEXIST] = { "EEXIST", "File exists" },
	[EXDEV] = { "EXDEV", "Invalid cross-device link" },
	[ENODEV] = { "ENODEV", "No such device" },
	[ENOTDIR] = { "ENOTDIR", "Not a directory" },
	[EISDIR] = { "EISDIR", "Is a directory" },
	[EINVAL] = { "EINVAL", "Invalid argument" },
	[ENFILE] = { "ENFILE", "Too many open files in system" },
	[EMFILE] = { "EMFILE", "Too many open files" },
	[ENOTTY] = { "ENOTTY", "Inappropriate ioctl for device" },
	[ETXTBSY] = { "ETXTBSY", "Text file busy" },
	[EFBIG] = { "EFBIG", "File too large" },
	[ENOSPC] = { "ENOSPC", "No space left on device" },
	[ESPIPE] = { "ESPIPE", "Illegal seek" },
	[EROFS] = { "EROFS", "Read-only file system" },
	[EMLINK] = { "EMLINK", "Too many links" },
	[EPIPE] = { "EPIPE", "Broken pipe" },
	[EDOM] = { "EDOM", "Numerical argument out of domain" },
	[ERANGE] = { "ERANGE", "Numerical result out of range" },
	[EDEADLK] = { "EDEADLK", "Resource deadlock avoided" },
	[ENAMETOOLONG] = { "ENAMETOOLONG", "File name too long" },
	[ENOLCK] = { "ENOLCK", "No locks available" },
	[ENOSYS] = { "ENOSYS", "Function not implemented" },
	[ENOTEMPTY] = { "ENOTEMPTY", "Directory not empty" },
	[ELOOP] = { "ELOOP", "Too many levels of symbolic links" },
	[ENOMSG] = { "ENOMSG", "No message of desired type" },
	[EIDRM] = { "EIDRM", "Identifier removed" },
	[ENOSTR] = { "ENOSTR", "Device not a stream" },
	[ENODATA] = { "ENODATA", "No data available" },
	[ETIME] = { "ETIME", "Timer expired" },
	[ENOSR] = { "ENOSR", "Out of streams resources" },
	[ENOLINK] = { "ENOLINK", "Link has been severed" },
	[EPROTO] = { "EPROTO", "Protocol error" },
	[EMULTIHOP] = { "EMULTIHOP", "Multihop attempted" },
	[EBADMSG] = { "EBADMSG", "Bad message" },
	[EOVERFLOW] = { "EOVERFLOW", "Value too large for defined data type" },
	[EILSEQ] = { "EILSEQ", "Invalid or incomplete multibyte or wide character" },
	[EUSERS] = { "EUSERS", "Too many users" },
	[ENOTSOCK] = { "ENOTSOCK", "Socket operation on non-socket" },
	[EDESTADDRREQ] = { "EDESTADDRREQ", "Destination address required" },
	[EMSGSIZE] = { "EMSGSIZE", "Message too long" },
	[EPROTOTYPE] = { "EPROTOTYPE", "Protocol wrong type for socket" },
	[ENOPROTOOPT] = { "ENOPROTOOPT", "Protocol not available" },
	[EPROTONOSUPPORT] = { "EPROTONOSUPPORT", "Protocol not supported" },
	[ESOCKTNOSUPPORT] = { "ESOCKTNOSUPPORT", "Socket type not supported" },
	[EOPNOTSUPP] = { "EOPNOTSUPP", "Operation not supported" },
	[EPFNOSUPPORT] = { "EPFNOSUPPORT", "Protocol family not supported" },
	[EAFNOSUPPORT] = { "EAFNOSUPPORT", "Address family not supported by protocol" },
	[EADDRINUSE] = { "EADDRINUSE", "Address already in use" },
	[EADDRNOTAVAIL] = { "EADDRNOTAVAIL", "Cannot assign requested address" },
	[ENETDOWN] = { "ENETDOWN", "Network is down" },
	[ENETUNREACH] = { "ENETUNREACH", "Network is unreachable" },
	[ENETRESET] = { "ENETRESET", "Network dropped connection on reset" },
	[ECONNABORTED] = { "ECONNABORTED", "Software caused connection abort" },
	[ECONNRESET] = { "ECONNRESET", "Connection reset by peer" },
	[ENOBUFS] = { "ENOBUFS", "No buffer space available" },
	[EISCONN] = { "EISCONN", "Transport endpoint is already connected" },
	[ENOTCONN] = { "ENOTCONN", "Transport endpoint is not connected" },
	[ESHUTDOWN] = { "ESHUTDOWN", "Cannot send after transport endpoint shutdown" },
	[ETOOMANYREFS] = { "ETOOMANYREFS", "Too many references: cannot splice" },
	[ETIMEDOUT] = { "ETIMEDOUT", "Connection timed out" },
	[ECONNREFUSED] = { "ECONNREFUSED", "Connection refused" },
	[EHOSTDOWN] = { "EHOSTDOWN", "Host is down" },
	[EHOSTUNREACH] = { "EHOSTUNREACH", "No route to host" },
	[EALREADY] = { "EALREADY", "Operation already in progress" },
	[EINPROGRESS] = { "EINPROGRESS", "Operation now in progress" },
	[ESTALE] = { "ESTALE", "Stale file handle" },
	[EDQUOT] = { "EDQUOT", "Disk quota exceeded" },
	[ECANCELED] = { "ECANCELED", "Operation canceled" },
	[EOWNERDEAD] = { "EOWNERDEAD", "Owner died" },
	[ENOTRECOVERABLE] = { "ENOTRECOVERABLE", "State not recoverable" },
};

static const char *err_msg(int e)
{
	if (e == 0)
		return "Success";
	if (e > 0 && (size_t)e < sizeof errtab / sizeof *errtab && errtab[e].msg)
		return errtab[e].msg;
	return 0;
}

/* strerror returns a pointer to constant storage for every input, so it
 * is thread-safe; unknown values share one generic message. */
char *strerror(int e)
{
	const char *m = err_msg(e);
	return (char *)(m ? m : "Unknown error");
}

int strerror_r(int e, char *buf, size_t n)
{
	const char *m = err_msg(e);
	int r = 0;
	char tmp[32];
	if (!m) {
		/* "Unknown error N" */
		char num[16], *p = num + sizeof num;
		unsigned u = e < 0 ? -(unsigned)e : (unsigned)e;
		*--p = 0;
		do *--p = (char)('0' + u % 10); while (u /= 10);
		if (e < 0)
			*--p = '-';
		strcpy(tmp, "Unknown error ");
		strcat(tmp, p);
		m = tmp;
		r = EINVAL;
	}
	size_t l = strlen(m);
	if (!n)
		return ERANGE;
	if (l >= n) {
		memcpy(buf, m, n - 1);
		buf[n - 1] = 0;
		return ERANGE;
	}
	memcpy(buf, m, l + 1);
	return r;
}

const char *strerrorname_np(int e)
{
	if (e == 0)
		return "0";
	if (e > 0 && (size_t)e < sizeof errtab / sizeof *errtab)
		return errtab[e].name;
	return 0;
}

static const char *const sigtab[] = {
	[SIGHUP] = "Hangup",
	[SIGINT] = "Interrupt",
	[SIGQUIT] = "Quit",
	[SIGILL] = "Illegal instruction",
	[SIGTRAP] = "Trace/breakpoint trap",
	[SIGABRT] = "Aborted",
	[SIGBUS] = "Bus error",
	[SIGFPE] = "Floating point exception",
	[SIGKILL] = "Killed",
	[SIGUSR1] = "User defined signal 1",
	[SIGSEGV] = "Segmentation fault",
	[SIGUSR2] = "User defined signal 2",
	[SIGPIPE] = "Broken pipe",
	[SIGALRM] = "Alarm clock",
	[SIGTERM] = "Terminated",
	[SIGSTKFLT] = "Stack fault",
	[SIGCHLD] = "Child exited",
	[SIGCONT] = "Continued",
	[SIGSTOP] = "Stopped (signal)",
	[SIGTSTP] = "Stopped",
	[SIGTTIN] = "Stopped (tty input)",
	[SIGTTOU] = "Stopped (tty output)",
	[SIGURG] = "Urgent I/O condition",
	[SIGXCPU] = "CPU time limit exceeded",
	[SIGXFSZ] = "File size limit exceeded",
	[SIGVTALRM] = "Virtual timer expired",
	[SIGPROF] = "Profiling timer expired",
	[SIGWINCH] = "Window changed",
	[SIGIO] = "I/O possible",
	[SIGPWR] = "Power failure",
	[SIGSYS] = "Bad system call",
};

char *strsignal(int s)
{
	if (s > 0 && (size_t)s < sizeof sigtab / sizeof *sigtab && sigtab[s])
		return (char *)sigtab[s];
	if (s >= 34 && s <= 64)
		return (char *)"Real-time signal";
	return (char *)"Unknown signal";
}
