/* Placeholder allocator: one private mapping per allocation.
 *
 * This exists so the rest of the library links and can be tested; it will
 * be replaced by an integrated hardened allocator (see README). It is slow
 * and wasteful, but strict:
 *
 *  - each block is placed at the end of its mapping, directly against a
 *    PROT_NONE guard page, so a linear overflow faults immediately;
 *  - free() unmaps, so most use-after-free faults;
 *  - the header in front of each block carries a tag keyed by the
 *    per-process pointer guard, and free()/realloc() abort on a mismatch
 *    (invalid or corrupted pointer). */
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/mman.h>

#define MIN_ALIGN 16

struct hdr {
	void *base;      /* start of the mapping */
	size_t map_len;  /* length of the mapping, including the guard page */
	size_t size;     /* requested size */
	uintptr_t tag;
};
_Static_assert(sizeof(struct hdr) % MIN_ALIGN == 0, "header keeps alignment");

static uintptr_t tag_of(const struct hdr *h, const void *p)
{
	uintptr_t t = (uintptr_t)h->base ^ (h->map_len << 7) ^ (h->size * 0x9e3779b97f4a7c15UL) ^ (uintptr_t)p;
	return __ptr_mangle(t);
}

static struct hdr *hdr_of(void *p, const char *who)
{
	if ((uintptr_t)p % MIN_ALIGN)
		__fatal(who);
	struct hdr *h = (struct hdr *)p - 1;
	if (h->tag != tag_of(h, p))
		__fatal(who);
	return h;
}

static void *alloc(size_t n, size_t align)
{
	if (n == 0)
		n = 1;
	if (align < MIN_ALIGN)
		align = MIN_ALIGN;
	if (n > PTRDIFF_MAX / 2 - align - sizeof(struct hdr) - 2 * PAGE_SZ) {
		errno = ENOMEM;
		return 0;
	}
	size_t len = ROUND_UP(n + align + sizeof(struct hdr), PAGE_SZ) + PAGE_SZ;
	unsigned char *base = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		return 0;
	unsigned char *guard = base + len - PAGE_SZ;
	if (mprotect(guard, PAGE_SZ, PROT_NONE)) {
		munmap(base, len);
		return 0;
	}
	/* Right-align, rounding down to the alignment; up to align-1 bytes
	 * of slack can remain before the guard page. */
	unsigned char *p = (unsigned char *)ROUND_DOWN((uintptr_t)(guard - n), align);
	struct hdr *h = (struct hdr *)p - 1;
	h->base = base;
	h->map_len = len;
	h->size = n;
	h->tag = tag_of(h, p);
	return p;
}

void *malloc(size_t n)
{
	return alloc(n, MIN_ALIGN);
}

void *calloc(size_t m, size_t n)
{
	size_t t;
	if (__builtin_mul_overflow(m, n, &t)) {
		errno = ENOMEM;
		return 0;
	}
	return alloc(t, MIN_ALIGN); /* fresh anonymous mappings are zeroed */
}

void free(void *p)
{
	if (!p)
		return;
	struct hdr *h = hdr_of(p, "free(): invalid pointer");
	void *base = h->base;
	size_t len = h->map_len;
	h->tag = 0;
	munmap(base, len);
}

void freezero(void *p, size_t n)
{
	if (!p)
		return;
	struct hdr *h = hdr_of(p, "freezero(): invalid pointer");
	explicit_bzero(p, n < h->size ? n : h->size);
	free(p);
}

void *realloc(void *p, size_t n)
{
	if (!p)
		return malloc(n);
	struct hdr *h = hdr_of(p, "realloc(): invalid pointer");
	void *q = malloc(n);
	if (!q)
		return 0;
	memcpy(q, p, h->size < n ? h->size : n);
	free(p);
	return q;
}

void *reallocarray(void *p, size_t m, size_t n)
{
	size_t t;
	if (__builtin_mul_overflow(m, n, &t)) {
		errno = ENOMEM;
		return 0;
	}
	return realloc(p, t);
}

void *aligned_alloc(size_t align, size_t n)
{
	if (!align || (align & (align - 1))) {
		errno = EINVAL;
		return 0;
	}
	return alloc(n, align);
}

int posix_memalign(void **res, size_t align, size_t n)
{
	if (align < sizeof(void *) || (align & (align - 1)))
		return EINVAL;
	int e = errno; /* posix_memalign reports through its return value only */
	void *p = alloc(n, align);
	if (!p) {
		errno = e;
		return ENOMEM;
	}
	*res = p;
	return 0;
}

void *memalign(size_t align, size_t n)
{
	return aligned_alloc(align, n);
}

void *valloc(size_t n)
{
	return alloc(n, PAGE_SZ);
}

void *pvalloc(size_t n)
{
	if (n > SIZE_MAX - PAGE_SZ) {
		errno = ENOMEM;
		return 0;
	}
	return alloc(ROUND_UP(n ? n : 1, PAGE_SZ), PAGE_SZ);
}

size_t malloc_usable_size(void *p)
{
	return p ? hdr_of(p, "malloc_usable_size(): invalid pointer")->size : 0;
}

int malloc_trim(size_t pad)
{
	return 0;
}
