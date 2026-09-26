/* dl_iterate_phdr and _dl_find_object for static executables: the
 * objects are the program itself and the vDSO if the kernel maps one (as
 * glibc reports it). The C++ unwinders use these to find PT_GNU_EH_FRAME
 * (LLVM's libunwind dl_iterate_phdr, GCC's libgcc_eh _dl_find_object), so
 * exceptions depend on them. */
#include "internal.h"
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/auxv.h>

extern const Elf64_Ehdr __ehdr_start __attribute__((__visibility__("hidden")));

static uintptr_t bias_of(const Elf64_Phdr *ph, size_t n, uintptr_t phdr_addr, uintptr_t fallback)
{
	for (size_t i = 0; i < n; i++)
		if (ph[i].p_type == PT_PHDR)
			return phdr_addr - ph[i].p_vaddr;
	return fallback;
}

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *arg)
{
	struct dl_phdr_info info = { 0 };
	const Elf64_Phdr *ph = (const Elf64_Phdr *)__libc.auxv[AT_PHDR];
	size_t n = __libc.auxv[AT_PHNUM];
	uintptr_t fallback = __ehdr_start.e_type == ET_DYN ? (uintptr_t)&__ehdr_start : 0;
	int objects = 1;
	const Elf64_Ehdr *vdso = (const Elf64_Ehdr *)__libc.auxv[AT_SYSINFO_EHDR];
	if (vdso)
		objects++;

	info.dlpi_addr = bias_of(ph, n, (uintptr_t)ph, fallback);
	info.dlpi_name = "";
	info.dlpi_phdr = ph;
	info.dlpi_phnum = (Elf64_Half)n;
	info.dlpi_adds = (unsigned long long)objects;
	info.dlpi_subs = 0;
	for (size_t i = 0; i < n; i++) {
		if (ph[i].p_type == PT_TLS && ph[i].p_memsz) {
			info.dlpi_tls_modid = 1;
			/* variant II: the block ends at the thread pointer */
			info.dlpi_tls_data = (char *)__self() - __libc.tls_offset;
		}
	}
	int r = cb(&info, sizeof info, arg);
	if (r || !vdso)
		return r;

	/* The vDSO: its load bias comes from the PT_LOAD that maps the
	 * start of the image. */
	const Elf64_Phdr *vph = (const Elf64_Phdr *)((const char *)vdso + vdso->e_phoff);
	uintptr_t vbias = (uintptr_t)vdso;
	for (size_t i = 0; i < vdso->e_phnum; i++) {
		if (vph[i].p_type == PT_LOAD && vph[i].p_offset == 0) {
			vbias = (uintptr_t)vdso - vph[i].p_vaddr;
			break;
		}
	}
	struct dl_phdr_info v = { 0 };
	v.dlpi_addr = vbias;
	v.dlpi_name = "linux-vdso.so.1";
	v.dlpi_phdr = vph;
	v.dlpi_phnum = vdso->e_phnum;
	v.dlpi_adds = (unsigned long long)objects;
	v.dlpi_subs = 0;
	return cb(&v, sizeof v, arg);
}

/* Look for pc in one object; fill r and return 1 if it is there. */
static int find_in(const Elf64_Phdr *ph, size_t n, uintptr_t bias, struct link_map *lm, uintptr_t pc,
                   struct dl_find_object *r)
{
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	int inside = 0;
	void *eh = 0;
	for (size_t i = 0; i < n; i++) {
		if (ph[i].p_type == PT_LOAD) {
			uintptr_t s = bias + ph[i].p_vaddr, e = s + ph[i].p_memsz;
			if (pc >= s && pc < e)
				inside = 1;
			if (s < lo)
				lo = s;
			if (e > hi)
				hi = e;
		} else if (ph[i].p_type == PT_GNU_EH_FRAME) {
			eh = (void *)(bias + ph[i].p_vaddr);
		}
	}
	if (!inside)
		return 0;
	*r = (struct dl_find_object){ 0 };
	r->dlfo_map_start = (void *)lo;
	r->dlfo_map_end = (void *)hi;
	r->dlfo_link_map = lm;
	r->dlfo_eh_frame = eh;
	return 1;
}

int _dl_find_object(void *pc, struct dl_find_object *r)
{
	static struct link_map exe_map, vdso_map;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)__libc.auxv[AT_PHDR];
	size_t n = __libc.auxv[AT_PHNUM];
	uintptr_t fallback = __ehdr_start.e_type == ET_DYN ? (uintptr_t)&__ehdr_start : 0;
	uintptr_t bias = bias_of(ph, n, (uintptr_t)ph, fallback);
	exe_map.l_addr = bias;
	exe_map.l_name = (char *)"";
	if (find_in(ph, n, bias, &exe_map, (uintptr_t)pc, r))
		return 0;
	const Elf64_Ehdr *vdso = (const Elf64_Ehdr *)__libc.auxv[AT_SYSINFO_EHDR];
	if (vdso) {
		const Elf64_Phdr *vph = (const Elf64_Phdr *)((const char *)vdso + vdso->e_phoff);
		uintptr_t vbias = (uintptr_t)vdso;
		for (size_t i = 0; i < vdso->e_phnum; i++) {
			if (vph[i].p_type == PT_LOAD && vph[i].p_offset == 0) {
				vbias = (uintptr_t)vdso - vph[i].p_vaddr;
				break;
			}
		}
		vdso_map.l_addr = vbias;
		vdso_map.l_name = (char *)"linux-vdso.so.1";
		if (find_in(vph, vdso->e_phnum, vbias, &vdso_map, (uintptr_t)pc, r))
			return 0;
	}
	return -1;
}
