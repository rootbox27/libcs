/* What OpenBSD's malloc.c (src/omalloc/malloc.c) expects from OpenBSD's
 * libc, mapped onto Citadel's runtime. Kept apart from the vendored file
 * so that file stays close to upstream. */
#ifndef OMALLOC_COMPAT_H
#define OMALLOC_COMPAT_H

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

/* the stats code needs ktrace, a tree library and dladdr */
#define MALLOC_SMALL

typedef unsigned short ushort;
#define NBBY 8
#define __dead __attribute__((__noreturn__))

#define _MAX_PAGE_SHIFT 12
_Static_assert((1 << _MAX_PAGE_SHIFT) == PAGE_SZ, "page size");

/* Symbol aliasing macros of OpenBSD's libc: not needed in a static libc. */
#define DEF_STRONG(x)
#define DEF_WEAK(x)
#define PROTO_NORMAL(x)

/* No MAP_CONCEAL on Linux. The pool it marks (malloc_conceal) is not
 * exported here; everything else maps memory without it anyway. */
#define MAP_CONCEAL 0

#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define _ALIGN(p) (((uintptr_t)(p) + 15) & ~(uintptr_t)15)

/* Growing a region in place maps the pages after it only if they are
 * free. MAP_FIXED is taken away: a kernel older than 4.17 ignores
 * MAP_FIXED_NOREPLACE, and with MAP_FIXED it would silently replace
 * whatever was mapped there. Without it the address is a hint, and
 * malloc.c already checks that the mapping landed where it asked. */
#undef MAP_FIXED
#define MAP_FIXED 0
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#define __MAP_NOREPLACE MAP_FIXED_NOREPLACE

/* ---- <sys/queue.h> lists, the subset malloc.c uses ---- */
#define LIST_HEAD(name, type) struct name { struct type *lh_first; }
#define LIST_ENTRY(type) struct { struct type *le_next; struct type **le_prev; }
#define LIST_FIRST(head) ((head)->lh_first)
#define LIST_NEXT(elm, field) ((elm)->field.le_next)
#define LIST_EMPTY(head) (LIST_FIRST(head) == NULL)
#define LIST_INIT(head) do { LIST_FIRST(head) = NULL; } while (0)
#define LIST_INSERT_HEAD(head, elm, field) do { \
	if (((elm)->field.le_next = (head)->lh_first) != NULL) \
		(head)->lh_first->field.le_prev = &(elm)->field.le_next; \
	(head)->lh_first = (elm); \
	(elm)->field.le_prev = &(head)->lh_first; \
} while (0)
#define LIST_REMOVE(elm, field) do { \
	if ((elm)->field.le_next != NULL) \
		(elm)->field.le_next->field.le_prev = (elm)->field.le_prev; \
	*(elm)->field.le_prev = (elm)->field.le_next; \
} while (0)

/* ---- locking: one futex lock per pool ---- */
#define _MALLOC_MUTEXES 32
hidden extern volatile int __omalloc_locks[_MALLOC_MUTEXES];
#define _MALLOC_LOCK(n) LOCK(__omalloc_locks[n])
#define _MALLOC_UNLOCK(n) UNLOCK(__omalloc_locks[n])

/* the thread id picks a thread's pool */
#define TIB_GET() __self()
#define tib_tid tid

/* the program name for error reports */
#define __progname program_invocation_short_name
extern char *program_invocation_short_name;


/* OpenBSD reads system-wide options from the vm.malloc_conf sysctl
 * first, then MALLOC_OPTIONS (not in setuid programs), then the program's
 * malloc_options. Citadel's defaults take the sysctl's place:
 *   C  canaries after small chunks, checked when they are freed
 *   G  a guard page after every page-sized allocation
 * so they apply unless MALLOC_OPTIONS or the program turns them off. */
#define CITADEL_MALLOC_DEFAULTS "CG"
#define CTL_VM 0
#define VM_MALLOC_CONF 0
static inline int sysctl(const int *mib, unsigned n, void *old, size_t *oldlen, void *new_, size_t newlen)
{
	if (*oldlen < sizeof CITADEL_MALLOC_DEFAULTS)
		return -1;
	memcpy(old, CITADEL_MALLOC_DEFAULTS, sizeof CITADEL_MALLOC_DEFAULTS);
	*oldlen = sizeof CITADEL_MALLOC_DEFAULTS;
	return 0;
}

/* mimmutable() seals the pool directory, the page caches and the option
 * page against later mprotect/munmap. Linux has mseal (6.10); on older
 * kernels the sealing is skipped rather than failing malloc. */
#ifndef SYS_mseal
#define SYS_mseal 462
#endif
static inline int mimmutable(void *addr, size_t len)
{
	long r = __sys(SYS_mseal, addr, len, 0);
	(void)r;
	return 0;
}

hidden void _malloc_init(int from_rthreads);

#endif
