#ifndef _LINK_H
#define _LINK_H
#include <features.h>
#include <elf.h>
#define __need_size_t
#include <stddef.h>
__BEGIN_DECLS

#define ElfW(type) Elf64_##type

struct dl_phdr_info {
	ElfW(Addr) dlpi_addr;           /* load bias */
	const char *dlpi_name;
	const ElfW(Phdr) *dlpi_phdr;
	ElfW(Half) dlpi_phnum;
	unsigned long long dlpi_adds;   /* objects loaded so far */
	unsigned long long dlpi_subs;   /* objects unloaded so far */
	size_t dlpi_tls_modid;          /* 0 if the object has no TLS */
	void *dlpi_tls_data;            /* this thread's TLS block, or 0 */
};

struct link_map {
	ElfW(Addr) l_addr;
	char *l_name;
	ElfW(Dyn) *l_ld;
	struct link_map *l_next, *l_prev;
};

/* The debugger interface: DT_DEBUG in the program points at _r_debug;
 * the dynamic linker calls r_brk (_dl_debug_state) before and after it
 * changes the object list, with r_state saying what is happening. */
struct r_debug {
	int r_version;
	struct link_map *r_map;
	ElfW(Addr) r_brk;
	enum { RT_CONSISTENT, RT_ADD, RT_DELETE } r_state;
	ElfW(Addr) r_ldbase;
};
extern struct r_debug _r_debug;
void _dl_debug_state(void);

int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *);

__END_DECLS
#endif
