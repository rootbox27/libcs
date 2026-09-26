/* The dynamic linker's view of a loaded object. */
#ifndef CITADEL_DYNLINK_H
#define CITADEL_DYNLINK_H
#include <elf.h>
#include <stddef.h>
#include <stdint.h>

struct dl_phdr_info;

struct dso {
	const char *name;
	uintptr_t base;                 /* load bias */
	const Elf64_Phdr *phdr;
	size_t phnum;
	const Elf64_Dyn *dynv;
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
	int tls_id;                     /* 0: no TLS */
	size_t tls_offset;              /* its block is at tp - tls_offset */
	struct dso *next;
};

hidden uintptr_t __dls_start(long *sp);
hidden void __dl_fini(void);
hidden int __dl_object(int i, struct dl_phdr_info *info);

#endif
