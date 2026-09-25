/* backtrace: follows the frame-pointer chain, so it sees only frames
 * compiled with frame pointers; it stops at the first frame that does not
 * look like one (outside the current stack, or not moving up it). */
#include <execinfo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int backtrace(void **buf, int size)
{
	uintptr_t *fp = __builtin_frame_address(0);
	uintptr_t lo = (uintptr_t)&fp, hi = lo + (8u << 20);
	int n = 0;
	while (n < size && fp && (uintptr_t)fp >= lo && (uintptr_t)fp < hi && !((uintptr_t)fp & 7)) {
		uintptr_t ret = fp[1];
		if (!ret)
			break;
		buf[n++] = (void *)ret;
		uintptr_t *next = (uintptr_t *)fp[0];
		if (next <= fp)
			break;
		fp = next;
	}
	return n;
}

char **backtrace_symbols(void *const *buf, int n)
{
	if (n <= 0)
		return 0;
	size_t each = 2 + 2 * sizeof(void *) + 1;
	char **v = malloc((size_t)n * (sizeof *v + each));
	if (!v)
		return 0;
	char *s = (char *)(v + n);
	for (int i = 0; i < n; i++, s += each) {
		snprintf(s, each, "%p", buf[i]);
		v[i] = s;
	}
	return v;
}

void backtrace_symbols_fd(void *const *buf, int n, int fd)
{
	for (int i = 0; i < n; i++)
		dprintf(fd, "%p\n", buf[i]);
}
