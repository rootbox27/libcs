/* Process startup: self-relocation (static-PIE), RELRO, TLS and the
 * thread control block (stack canary + pointer guard), secure-mode checks,
 * constructors, main().
 *
 * The helpers here are shared by the static startup at the end of this
 * file and by the dynamic linker (ldso/dynlink.c) in libc.so.
 *
 * NOTE: everything up to __setup_tcb() runs before relocations are
 * applied and before %fs is valid. This file is built without the stack
 * protector and those helpers use only raw system calls, their own loops
 * and RIP-relative (hidden or static) data. */
#include "internal.h"
#include <elf.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>

hidden struct libc_state __libc;
char **__environ;
weak_alias(__environ, environ);
char *program_invocation_name, *program_invocation_short_name;

hidden size_t __auxv_copy[AUX_CNT];

hidden __attribute__((__noreturn__)) void __early_die(void)
{
	__syscall1(SYS_exit_group, 127);
	for (;;) ;
}

/* Apply the relative relocations of the object loaded at base. With
 * strict, anything else is fatal (a static-PIE has nothing else); without
 * it, symbolic relocations are left for the dynamic linker. */
hidden void __self_relocate(uintptr_t base, const Elf64_Dyn *dyn, int strict)
{
	uintptr_t rela = 0, relr = 0;
	size_t relasz = 0, relrsz = 0;
	for (; dyn->d_tag != DT_NULL; dyn++) {
		switch (dyn->d_tag) {
		case DT_RELA: rela = dyn->d_un.d_ptr; break;
		case DT_RELASZ: relasz = dyn->d_un.d_val; break;
		case DT_RELR: relr = dyn->d_un.d_ptr; break;
		case DT_RELRSZ: relrsz = dyn->d_un.d_val; break;
		}
	}
	if (rela) {
		const Elf64_Rela *r = (const Elf64_Rela *)(base + rela);
		for (size_t i = 0; i < relasz / sizeof *r; i++) {
			unsigned long t = ELF64_R_TYPE(r[i].r_info);
			if (t == R_X86_64_RELATIVE)
				*(uintptr_t *)(base + r[i].r_offset) = base + r[i].r_addend;
			else if (t != R_X86_64_NONE && strict)
				__early_die();
		}
	}
	if (relr) {
		const Elf64_Relr *r = (const Elf64_Relr *)(base + relr);
		uintptr_t *where = 0;
		for (size_t i = 0; i < relrsz / sizeof *r; i++) {
			Elf64_Relr e = r[i];
			if ((e & 1) == 0) {
				where = (uintptr_t *)(base + e);
				*where++ += base;
			} else {
				for (int j = 0; (e >>= 1) != 0; j++)
					if (e & 1)
						where[j] += base;
				where += 63;
			}
		}
	}
}

hidden void __init_tp(struct pthread *p)
{
	p->self = p;
	if (__syscall2(SYS_arch_prctl, 0x1002 /* ARCH_SET_FS */, (long)p) < 0)
		__early_die();
	p->tid = (int)__syscall1(SYS_set_tid_address, (long)&p->exit_futex);
	p->exit_futex = p->tid;
	p->tsd = p->tsd_storage;
}

/* ---- static TLS: one block per module below the thread pointer ----
 *
 * Variant II: the first module (the executable) ends at the thread
 * pointer; each further module lies below the previous one. The static
 * linker resolves the executable's TLS references to tp - round_up(memsz,
 * p_align), so every offset uses its segment's own alignment. The TCB
 * gets at least cache-line alignment, which (all being powers of two)
 * keeps every tp - offset aligned to its module's p_align. */

hidden int __tls_add(const void *image, size_t filesz, size_t memsz, size_t align)
{
	if (__libc.tls_count >= TLS_MODS_MAX)
		__early_die();
	struct tls_mod *m = &__libc.tls_mods[__libc.tls_count];
	m->image = image;
	m->filesz = filesz;
	m->memsz = memsz;
	m->align = align ? align : 1;
	return ++__libc.tls_count; /* module ids start at 1 */
}

hidden void __tls_layout(void)
{
	size_t off = 0, align = 64;
	for (int i = 0; i < __libc.tls_count; i++) {
		struct tls_mod *m = &__libc.tls_mods[i];
		off = ROUND_UP(off + m->memsz, m->align);
		m->offset = off;
		if (m->align > align)
			align = m->align;
	}
	__libc.tls_offset = off;
	__libc.tls_align = align;
}

/* Initialise the TLS blocks of a new thread (the memory is zeroed). */
hidden void __copy_tls(uintptr_t tp)
{
	for (int i = 0; i < __libc.tls_count; i++) {
		const struct tls_mod *m = &__libc.tls_mods[i];
		unsigned char *d = (unsigned char *)(tp - m->offset);
		const unsigned char *s = m->image;
		for (size_t k = 0; k < m->filesz; k++)
			d[k] = s[k];
	}
}

/* Map the main thread's TLS and TCB, set the stack canary and pointer
 * guard from AT_RANDOM, and install the thread pointer. */
hidden void __setup_tcb(const unsigned char *rnd)
{
	size_t align = __libc.tls_align, off = __libc.tls_offset;
	size_t total = ROUND_UP(off + sizeof(struct pthread) + align, PAGE_SZ);
	long m = __syscall6(SYS_mmap, 0, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m < 0 && m > -4096)
		__early_die();
	uintptr_t tp = ROUND_UP((uintptr_t)m + off, align);
	__copy_tls(tp);

	struct pthread *p = (struct pthread *)tp;
	uintptr_t c = 0, g = 0;
	for (int i = 0; i < 8; i++) {
		c |= (uintptr_t)rnd[i] << (8 * i);
		g |= (uintptr_t)rnd[8 + i] << (8 * i);
	}
	/* Low byte zero stops string functions from reading or writing past
	 * the canary. */
	c &= ~(uintptr_t)0xff;
	p->canary = c;
	p->ptr_guard = g;
	p->map_base = (void *)m;
	p->map_size = total;
	__libc.canary = c;
	__libc.ptr_guard = g;
	__init_tp(p);
}

hidden void __apply_relro(uintptr_t base, const Elf64_Phdr *ph, size_t phnum)
{
	for (size_t i = 0; i < phnum; i++) {
		if (ph[i].p_type != PT_GNU_RELRO)
			continue;
		uintptr_t s = ROUND_DOWN(base + ph[i].p_vaddr, PAGE_SZ);
		uintptr_t e = ROUND_DOWN(base + ph[i].p_vaddr + ph[i].p_memsz, PAGE_SZ);
		if (e > s)
			__syscall3(SYS_mprotect, s, e - s, PROT_READ);
	}
}

/* For setuid/setgid programs make sure fds 0-2 are open, so that a later
 * open() cannot land on stdout/stderr and receive attacker output. */
static void secure_fds(void)
{
	struct pollfd pfd[3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
	long r;
	do r = __syscall3(SYS_poll, (long)pfd, 3, 0);
	while (r == -4 /* EINTR */);
	if (r < 0)
		__early_die();
	for (int i = 0; i < 3; i++) {
		if (!(pfd[i].revents & POLLNVAL))
			continue;
		long fd = __syscall3(SYS_open, (long)"/dev/null", O_RDWR, 0);
		if (fd != i)
			__early_die();
	}
}

/* Parse the auxiliary vector that follows envp. */
hidden size_t *__auxv_of(char **envp, size_t *aux)
{
	char **e = envp;
	while (*e)
		e++;
	size_t *auxv = (size_t *)(e + 1);
	for (int i = 0; i < AUX_CNT; i++)
		aux[i] = 0;
	for (size_t *a = auxv; a[0]; a += 2)
		if (a[0] < AUX_CNT)
			aux[a[0]] = a[1];
	return auxv;
}

/* Wipe AT_RANDOM once the canary and guard are taken from it. */
hidden void __wipe_random(const size_t *aux)
{
	if (aux[AT_RANDOM]) {
		volatile unsigned char *r = (volatile unsigned char *)aux[AT_RANDOM];
		for (int i = 0; i < 16; i++)
			r[i] = 0;
	}
}

/* The rest of libc's state; relocations must be complete (environ and
 * the program name may be copied into the executable). */
hidden void __init_libc(int argc, char **argv, char **envp, const size_t *aux)
{
	for (int i = 0; i < AUX_CNT; i++)
		__auxv_copy[i] = aux[i];
	__libc.auxv = __auxv_copy;
	__libc.secure = aux[AT_SECURE] != 0 || aux[AT_UID] != aux[AT_EUID] || aux[AT_GID] != aux[AT_EGID];
	if (__libc.secure)
		secure_fds();

	__environ = envp;
	if (argc > 0 && argv[0]) {
		program_invocation_name = argv[0];
		char *s = argv[0];
		for (char *p = argv[0]; *p; p++)
			if (*p == '/')
				s = p + 1;
		program_invocation_short_name = s;
		__libc.progname = s;
	} else {
		program_invocation_name = program_invocation_short_name = (char *)"";
		__libc.progname = "";
	}
}

typedef int (*main_fn)(int, char **, char **);

#ifndef CITADEL_SHARED

extern const Elf64_Ehdr __ehdr_start __attribute__((__visibility__("hidden")));
extern void (*const __preinit_array_start[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __preinit_array_end[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __init_array_start[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __init_array_end[])(void) __attribute__((__visibility__("hidden"), __weak__));

/* Static executables: crt1's _start comes here with the initial stack. */
__attribute__((__noreturn__, __used__)) void __citadel_start_c(long *sp, main_fn main)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	size_t aux[AUX_CNT];
	__auxv_of(envp, aux);

	uintptr_t base = 0;
	if (__ehdr_start.e_type == ET_DYN)
		base = (uintptr_t)&__ehdr_start;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)aux[AT_PHDR];
	size_t phnum = aux[AT_PHNUM];

	for (size_t i = 0; i < phnum; i++)
		if (ph[i].p_type == PT_DYNAMIC)
			__self_relocate(base, (const Elf64_Dyn *)(base + ph[i].p_vaddr), 1);

	for (size_t i = 0; i < phnum; i++)
		if (ph[i].p_type == PT_TLS)
			__tls_add((const void *)(base + ph[i].p_vaddr), ph[i].p_filesz, ph[i].p_memsz, ph[i].p_align);
	__tls_layout();
	static const unsigned char zero_rnd[16];
	__setup_tcb(aux[AT_RANDOM] ? (const unsigned char *)aux[AT_RANDOM] : zero_rnd);
	__wipe_random(aux);

	__apply_relro(base, ph, phnum);
	__init_libc(argc, argv, envp, aux);

	size_t n = __preinit_array_end - __preinit_array_start;
	for (size_t i = 0; i < n; i++)
		__preinit_array_start[i]();
	n = __init_array_end - __init_array_start;
	for (size_t i = 0; i < n; i++)
		__init_array_start[i]();

	exit(main(argc, argv, envp));
}

#else

/* Dynamically linked executables: the dynamic linker has already done
 * all of the above, including running every constructor, before it
 * jumped to the program's _start. */
__attribute__((__noreturn__, __used__)) void __citadel_start_c(long *sp, main_fn main)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	exit(main(argc, argv, argv + argc + 1));
}

#endif

unsigned long getauxval(unsigned long type)
{
	if (type < AUX_CNT && __libc.auxv)
		return __libc.auxv[type];
	return 0;
}

int issetugid(void)
{
	return __libc.secure;
}
