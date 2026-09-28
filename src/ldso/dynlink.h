/* The dynamic linker's view of a loaded object. */
#ifndef CITADEL_DYNLINK_H
#define CITADEL_DYNLINK_H
#include <elf.h>
#include <stddef.h>
#include <stdint.h>

struct dl_phdr_info;

struct dso {
	/* struct link_map, as debuggers read it through _r_debug.r_map */
	uintptr_t base;                 /* load bias */
	char *name;                     /* "" for the program */
	const Elf64_Dyn *dynv;
	struct dso *next, *prev;        /* every object, in load order */

	const char *path;               /* the file, for $ORIGIN */
	const Elf64_Phdr *phdr;
	size_t phnum;
	const Elf64_Sym *syms;
	const char *strings;
	const uint32_t *hashtab, *ghashtab;
	const Elf64_Rela *rela, *jmprel;
	size_t relasz, jmprelsz;
	const Elf64_Relr *relr;
	size_t relrsz;
	uintptr_t relro_start, relro_end;
	uintptr_t init, fini;
	void (**init_array)(void), (**fini_array)(void), (**preinit_array)(void);
	size_t init_n, fini_n, preinit_n;
	unsigned long flags_1;          /* DT_FLAGS_1 */
	int tls_id;                     /* 0: no TLS */
	size_t tls_offset;              /* its block is at tp - tls_offset */
	const char *shortname;          /* the name it was asked for by */
	void *map;                      /* the address range it occupies */
	size_t map_len;
	unsigned long dev, ino;
	struct dso **deps;              /* its DT_NEEDED, loaded */
	int ndeps;
	/* dlopen: the dependency closure of a handle, breadth first (the
	 * handle first); lscope is the closure that was being loaded when
	 * this object was, searched after the global scope while it is
	 * relocated */
	struct dso **scope, **lscope;
	int nscope, nlscope;
	int refcnt;                     /* dlopen handles whose closure holds it */
	int opens;                      /* dlopen calls that returned it, not closed */
	struct dso *fini_next;          /* destructor order */
	unsigned mark;
	unsigned char global;           /* in the global symbol scope */
	unsigned char permanent;        /* loaded at startup */
	unsigned char nodelete;         /* never unloaded */
	unsigned char visited, relocated, inited, allocated, doomed;
};

hidden void __dls_relocate_self(long *sp);
hidden uintptr_t __dls_start(long **spp);
hidden void __dl_fini(void);

#endif
