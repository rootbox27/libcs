/* dl_iterate_phdr and _dl_find_object. The objects are the executable
 * (static) or everything the dynamic linker loaded (libc.so), followed by
 * the vDSO if the kernel maps one, as glibc reports it. The C++ unwinders
 * use these to find PT_GNU_EH_FRAME (LLVM's libunwind dl_iterate_phdr,
 * GCC's libgcc_eh _dl_find_object), so exceptions depend on them. */
#include "internal.h"
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/auxv.h>
#ifdef CITADEL_SHARED
#include "ldso/dynlink.h"
#endif

extern const Elf64_Ehdr __ehdr_start __attribute__((__visibility__("hidden")));

static int vdso_info(struct dl_phdr_info *info)
{
	const Elf64_Ehdr *vdso = (const Elf64_Ehdr *)__libc.auxv[AT_SYSINFO_EHDR];
	if (!vdso)
		return 0;
	const Elf64_Phdr *vph = (const Elf64_Phdr *)((const char *)vdso + vdso->e_phoff);
	/* the load bias comes from the PT_LOAD mapping the image's start */
	uintptr_t bias = (uintptr_t)vdso;
	for (size_t i = 0; i < vdso->e_phnum; i++) {
		if (vph[i].p_type == PT_LOAD && vph[i].p_offset == 0) {
			bias = (uintptr_t)vdso - vph[i].p_vaddr;
			break;
		}
	}
	*info = (struct dl_phdr_info){ 0 };
	info->dlpi_addr = bias;
	info->dlpi_name = "linux-vdso.so.1";
	info->dlpi_phdr = vph;
	info->dlpi_phnum = vdso->e_phnum;
	return 1;
}

#ifndef CITADEL_SHARED
static int loaded_object(int i, struct dl_phdr_info *info)
{
	if (i)
		return 0;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)__libc.auxv[AT_PHDR];
	size_t n = __libc.auxv[AT_PHNUM];
	uintptr_t bias = __ehdr_start.e_type == ET_DYN ? (uintptr_t)&__ehdr_start : 0;
	for (size_t k = 0; k < n; k++)
		if (ph[k].p_type == PT_PHDR)
			bias = (uintptr_t)ph - ph[k].p_vaddr;
	*info = (struct dl_phdr_info){ 0 };
	info->dlpi_addr = bias;
	info->dlpi_name = "";
	info->dlpi_phdr = ph;
	info->dlpi_phnum = (Elf64_Half)n;
	for (size_t k = 0; k < n; k++) {
		if (ph[k].p_type == PT_TLS && ph[k].p_memsz) {
			info->dlpi_tls_modid = 1;
			/* variant II: the block ends at the thread pointer */
			info->dlpi_tls_data = (char *)__self() - __libc.tls_mods[0].offset;
		}
	}
	return 1;
}
#else
static int loaded_object(int i, struct dl_phdr_info *info)
{
	return __dl_object(i, info);
}
#endif

/* The i-th object: the loaded ones, then the vDSO. */
static int object(int i, struct dl_phdr_info *info, int *count)
{
	int n = 0;
	struct dl_phdr_info tmp;
	while (loaded_object(n, &tmp))
		n++;
	*count = n + (__libc.auxv[AT_SYSINFO_EHDR] != 0);
	if (i < n)
		return loaded_object(i, info);
	return i == n && vdso_info(info);
}

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *arg)
{
	struct dl_phdr_info info;
	int count;
	for (int i = 0; object(i, &info, &count); i++) {
		info.dlpi_adds = (unsigned long long)count;
		info.dlpi_subs = 0;
		int r = cb(&info, sizeof info, arg);
		if (r)
			return r;
	}
	return 0;
}

int _dl_find_object(void *pc, struct dl_find_object *r)
{
	static struct link_map maps[TLS_MODS_MAX + 2];
	struct dl_phdr_info info;
	int count;
	for (int i = 0; object(i, &info, &count) && i < (int)(sizeof maps / sizeof *maps); i++) {
		uintptr_t lo = UINTPTR_MAX, hi = 0;
		int inside = 0;
		void *eh = 0;
		for (size_t k = 0; k < info.dlpi_phnum; k++) {
			const Elf64_Phdr *p = &info.dlpi_phdr[k];
			if (p->p_type == PT_LOAD) {
				uintptr_t s = info.dlpi_addr + p->p_vaddr, e = s + p->p_memsz;
				if ((uintptr_t)pc >= s && (uintptr_t)pc < e)
					inside = 1;
				if (s < lo)
					lo = s;
				if (e > hi)
					hi = e;
			} else if (p->p_type == PT_GNU_EH_FRAME) {
				eh = (void *)(info.dlpi_addr + p->p_vaddr);
			}
		}
		if (!inside)
			continue;
		maps[i].l_addr = info.dlpi_addr;
		maps[i].l_name = (char *)info.dlpi_name;
		*r = (struct dl_find_object){ 0 };
		r->dlfo_map_start = (void *)lo;
		r->dlfo_map_end = (void *)hi;
		r->dlfo_link_map = &maps[i];
		r->dlfo_eh_frame = eh;
		return 0;
	}
	return -1;
}
