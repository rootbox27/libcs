/* BSD err/warn. */
#include "internal.h"
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vwarn(const char *fmt, va_list ap)
{
	int e = errno;
	flockfile(stderr);
	fprintf(stderr, "%s: ", __libc.progname);
	if (fmt) {
		vfprintf(stderr, fmt, ap);
		fputs(": ", stderr);
	}
	fprintf(stderr, "%s\n", strerror(e));
	funlockfile(stderr);
}

void vwarnx(const char *fmt, va_list ap)
{
	flockfile(stderr);
	fprintf(stderr, "%s: ", __libc.progname);
	if (fmt)
		vfprintf(stderr, fmt, ap);
	putc('\n', stderr);
	funlockfile(stderr);
}

void verr(int status, const char *fmt, va_list ap)
{
	vwarn(fmt, ap);
	exit(status);
}

void verrx(int status, const char *fmt, va_list ap)
{
	vwarnx(fmt, ap);
	exit(status);
}

void warn(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vwarn(fmt, ap); va_end(ap); }
void warnx(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vwarnx(fmt, ap); va_end(ap); }
void err(int s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); verr(s, fmt, ap); }
void errx(int s, const char *fmt, ...) { va_list ap; va_start(ap, fmt); verrx(s, fmt, ap); }
