/* Process startup: self-relocation (static-PIE), RELRO, TLS and the
 * thread control block (stack canary + pointer guard), secure-mode checks,
 * constructors, main().
 *
 * NOTE: everything here up to __init_tp() runs before relocations are
 * applied and before %fs is valid. This file is built without the stack
 * protector and must only use raw system calls and RIP-relative data. */
#include "internal.h"
#include <elf.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>

#define AUX_CNT 64

hidden struct libc_state __libc;
char **__environ;
weak_alias(__environ, environ);
char *program_invocation_name, *program_invocation_short_name;

extern const Elf64_Ehdr __ehdr_start __attribute__((__visibility__("hidden")));
extern void (*const __preinit_array_start[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __preinit_array_end[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __init_array_start[])(void) __attribute__((__visibility__("hidden"), __weak__));
extern void (*const __init_array_end[])(void) __attribute__((__visibility__("hidden"), __weak__));

int main(int, char **, char **);

static struct pthread *main_tcb;
static size_t auxv_copy[AUX_CNT];

static __attribute__((__noreturn__)) void early_die(void)
{
	__syscall1(SYS_exit_group, 127);
	for (;;) ;
}

static void self_relocate(uintptr_t base, const Elf64_Phdr *ph, size_t phnum)
{
	const Elf64_Dyn *dyn = 0;
	for (size_t i = 0; i < phnum; i++)
		if (ph[i].p_type == PT_DYNAMIC)
			dyn = (const Elf64_Dyn *)(base + ph[i].p_vaddr);
	if (!dyn)
		return;
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
			else if (t != R_X86_64_NONE)
				early_die(); /* symbolic relocs are not supported */
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
		early_die();
	p->tid = (int)__syscall1(SYS_set_tid_address, (long)&p->exit_futex);
	p->exit_futex = p->tid;
	p->tsd = p->tsd_storage;
}

static void init_tls(uintptr_t base, const Elf64_Phdr *ph, size_t phnum, const unsigned char *rnd)
{
	const Elf64_Phdr *tls = 0;
	for (size_t i = 0; i < phnum; i++)
		if (ph[i].p_type == PT_TLS)
			tls = &ph[i];
	size_t seg_align = 1, memsz = 0, filesz = 0;
	const void *image = 0;
	if (tls) {
		if (tls->p_align > seg_align)
			seg_align = tls->p_align;
		memsz = tls->p_memsz;
		filesz = tls->p_filesz;
		image = (const void *)(base + tls->p_vaddr);
	}
	/* Variant II: the TLS block sits immediately below the TCB. The
	 * static linker resolves TLS references to tp - round_up(memsz,
	 * p_align), so the offset must use the segment's own alignment. The
	 * TCB itself gets at least cache-line alignment, which (both being
	 * powers of two) keeps tp - off aligned to p_align as well. */
	size_t align = seg_align > 64 ? seg_align : 64;
	size_t off = ROUND_UP(memsz, seg_align);
	size_t total = ROUND_UP(off + sizeof(struct pthread) + align, PAGE_SZ);
	__libc.tls_size = memsz;
	__libc.tls_file_size = filesz;
	__libc.tls_align = align;
	__libc.tls_offset = off;
	__libc.tls_image = image;

	long m = __syscall6(SYS_mmap, 0, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m < 0 && m > -4096)
		early_die();
	uintptr_t tp = ROUND_UP((uintptr_t)m + off, align);
	unsigned char *blk = (unsigned char *)(tp - off);
	const unsigned char *src = image;
	for (size_t i = 0; i < filesz; i++)
		blk[i] = src[i];

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
	main_tcb = p;
}

static void apply_relro(uintptr_t base, const Elf64_Phdr *ph, size_t phnum)
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
		early_die();
	for (int i = 0; i < 3; i++) {
		if (!(pfd[i].revents & POLLNVAL))
			continue;
		long fd = __syscall3(SYS_open, (long)"/dev/null", O_RDWR, 0);
		if (fd != i)
			early_die();
	}
}

__attribute__((__noreturn__, __used__)) void __citadel_start_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	char **e = envp;
	while (*e)
		e++;
	size_t *auxv = (size_t *)(e + 1);
	size_t aux[AUX_CNT];
	for (int i = 0; i < AUX_CNT; i++)
		aux[i] = 0;
	for (size_t *a = auxv; a[0]; a += 2)
		if (a[0] < AUX_CNT)
			aux[a[0]] = a[1];

	uintptr_t base = 0;
	if (__ehdr_start.e_type == ET_DYN)
		base = (uintptr_t)&__ehdr_start;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)aux[AT_PHDR];
	size_t phnum = aux[AT_PHNUM];

	self_relocate(base, ph, phnum);

	static const unsigned char zero_rnd[16];
	const unsigned char *rnd = aux[AT_RANDOM] ? (const unsigned char *)aux[AT_RANDOM] : zero_rnd;
	init_tls(base, ph, phnum, rnd);
	/* Destroy AT_RANDOM so the canary secret is not left on the stack. */
	if (aux[AT_RANDOM]) {
		volatile unsigned char *r = (volatile unsigned char *)aux[AT_RANDOM];
		for (int i = 0; i < 16; i++)
			r[i] = 0;
	}

	apply_relro(base, ph, phnum);

	for (int i = 0; i < AUX_CNT; i++)
		auxv_copy[i] = aux[i];
	__libc.auxv = auxv_copy;
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

	size_t n = __preinit_array_end - __preinit_array_start;
	for (size_t i = 0; i < n; i++)
		__preinit_array_start[i]();
	n = __init_array_end - __init_array_start;
	for (size_t i = 0; i < n; i++)
		__init_array_start[i]();

	exit(main(argc, argv, envp));
}

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
