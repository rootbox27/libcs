/* The dynamic linker. libc.so is its own dynamic linker (programs name it
 * as their interpreter), so libc and the loader share one copy of their
 * state: TLS, malloc, errno.
 *
 * Policy:
 *  - every symbol is bound at startup (no lazy binding), and the
 *    relocated data (GOTs included) is then made read-only (RELRO);
 *  - text relocations and IFUNCs are refused;
 *  - no symbol versioning: programs are built against this libc.
 *
 * Stage 1: the program and libc. A program needing any other shared
 * library is refused with a clear message.
 *
 * Built only into libc.so (CITADEL_SHARED), without the stack protector:
 * __dls_start runs before the thread pointer exists. */
#ifdef CITADEL_SHARED
#include "internal.h"
#include "ldso/dynlink.h"
#include <elf.h>
#include <link.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

static struct dso app, self;
hidden struct dso *__dso_head;

/* ---- messages (raw writes: usable at any point) ---- */

static void say(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	__syscall3(SYS_write, 2, (long)s, (long)n);
}

static __attribute__((__noreturn__)) void fatal(const char *a, const char *b, const char *c)
{
	say("libc.so: ");
	say(a);
	if (b) {
		say(b);
		if (c)
			say(c);
	}
	say("\n");
	__syscall1(SYS_exit_group, 127);
	for (;;) ;
}

/* ---- reading an object's dynamic section ---- */

static void decode(struct dso *d)
{
	for (size_t i = 0; i < d->phnum; i++) {
		const Elf64_Phdr *p = &d->phdr[i];
		if (p->p_type == PT_DYNAMIC)
			d->dynv = (const Elf64_Dyn *)(d->base + p->p_vaddr);
		else if (p->p_type == PT_GNU_RELRO) {
			d->relro_start = d->base + p->p_vaddr;
			d->relro_end = d->relro_start + p->p_memsz;
		}
	}
	if (!d->dynv)
		fatal("no dynamic section in ", d->name, 0);
	for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++) {
		uintptr_t a = d->base + v->d_un.d_ptr;
		switch (v->d_tag) {
		case DT_SYMTAB: d->syms = (const Elf64_Sym *)a; break;
		case DT_STRTAB: d->strings = (const char *)a; break;
		case DT_HASH: d->hashtab = (const uint32_t *)a; break;
		case DT_GNU_HASH: d->ghashtab = (const uint32_t *)a; break;
		case DT_RELA: d->rela = (const Elf64_Rela *)a; break;
		case DT_RELASZ: d->relasz = v->d_un.d_val; break;
		case DT_JMPREL: d->jmprel = (const Elf64_Rela *)a; break;
		case DT_PLTRELSZ: d->jmprelsz = v->d_un.d_val; break;
		case DT_RELR: d->relr = (const Elf64_Relr *)a; break;
		case DT_RELRSZ: d->relrsz = v->d_un.d_val; break;
		case DT_INIT: d->init = a; break;
		case DT_FINI: d->fini = a; break;
		case DT_INIT_ARRAY: d->init_array = (void (**)(void))a; break;
		case DT_INIT_ARRAYSZ: d->init_n = v->d_un.d_val / sizeof(void *); break;
		case DT_FINI_ARRAY: d->fini_array = (void (**)(void))a; break;
		case DT_FINI_ARRAYSZ: d->fini_n = v->d_un.d_val / sizeof(void *); break;
		case DT_PREINIT_ARRAY: d->preinit_array = (void (**)(void))a; break;
		case DT_PREINIT_ARRAYSZ: d->preinit_n = v->d_un.d_val / sizeof(void *); break;
		case DT_TEXTREL:
			fatal("text relocations are not supported: ", d->name, 0);
		case DT_FLAGS:
			if (v->d_un.d_val & DF_TEXTREL)
				fatal("text relocations are not supported: ", d->name, 0);
			break;
		}
	}
	if (!d->syms || !d->strings || (!d->hashtab && !d->ghashtab))
		fatal("incomplete dynamic section in ", d->name, 0);
}

/* ---- symbol lookup ---- */

static uint32_t sysv_hash(const char *s)
{
	uint32_t h = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		h = 16 * h + *p;
		h ^= h >> 24 & 0xf0;
	}
	return h & 0xfffffff;
}

static uint32_t gnu_hash(const char *s)
{
	uint32_t h = 5381;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		h = h * 33 + *p;
	return h;
}

static int defined(const Elf64_Sym *s)
{
	int type = ELF64_ST_TYPE(s->st_info), bind = ELF64_ST_BIND(s->st_info);
	return s->st_shndx != SHN_UNDEF && (bind == STB_GLOBAL || bind == STB_WEAK) &&
	       (type == STT_NOTYPE || type == STT_OBJECT || type == STT_FUNC || type == STT_COMMON ||
	        type == STT_TLS);
}

static const Elf64_Sym *lookup_in(const struct dso *d, const char *name, uint32_t gh, uint32_t sh)
{
	if (d->ghashtab) {
		const uint32_t *ht = d->ghashtab;
		uint32_t nbuckets = ht[0], symoffset = ht[1], bloom_size = ht[2], shift = ht[3];
		const uint64_t *bloom = (const uint64_t *)(ht + 4);
		uint64_t word = bloom[(gh / 64) % bloom_size];
		uint64_t mask = (1ul << (gh % 64)) | (1ul << ((gh >> shift) % 64));
		if ((word & mask) != mask)
			return 0;
		const uint32_t *buckets = (const uint32_t *)(bloom + bloom_size);
		const uint32_t *chain = buckets + nbuckets;
		uint32_t i = buckets[gh % nbuckets];
		if (!i)
			return 0;
		for (;; i++) {
			uint32_t h2 = chain[i - symoffset];
			const Elf64_Sym *s = &d->syms[i];
			if ((gh | 1) == (h2 | 1) && defined(s) && !strcmp(name, d->strings + s->st_name))
				return s;
			if (h2 & 1)
				return 0;
		}
	}
	const uint32_t *ht = d->hashtab;
	uint32_t nbucket = ht[0];
	const uint32_t *bucket = ht + 2, *chain = bucket + nbucket;
	for (uint32_t i = bucket[sh % nbucket]; i; i = chain[i]) {
		const Elf64_Sym *s = &d->syms[i];
		if (defined(s) && !strcmp(name, d->strings + s->st_name))
			return s;
	}
	return 0;
}

/* The first definition in search order (the program, then its
 * libraries), skipping `skip` (for COPY relocations). */
static const Elf64_Sym *lookup(const char *name, const struct dso *skip, struct dso **where)
{
	uint32_t gh = gnu_hash(name), sh = sysv_hash(name);
	for (struct dso *d = __dso_head; d; d = d->next) {
		if (d == skip)
			continue;
		const Elf64_Sym *s = lookup_in(d, name, gh, sh);
		if (s) {
			*where = d;
			return s;
		}
	}
	return 0;
}

/* ---- relocation ---- */

static void relocate_table(struct dso *d, const Elf64_Rela *r, size_t size, int skip_relative)
{
	for (size_t i = 0; i < size / sizeof *r; i++) {
		unsigned type = (unsigned)ELF64_R_TYPE(r[i].r_info);
		unsigned symidx = (unsigned)ELF64_R_SYM(r[i].r_info);
		uintptr_t *where = (uintptr_t *)(d->base + r[i].r_offset);
		if (type == R_X86_64_NONE)
			continue;
		if (type == R_X86_64_RELATIVE) {
			if (!skip_relative)
				*where = d->base + r[i].r_addend;
			continue;
		}
		const Elf64_Sym *sym = symidx ? &d->syms[symidx] : 0;
		const Elf64_Sym *def = 0;
		struct dso *dd = d;
		if (sym) {
			const char *name = d->strings + sym->st_name;
			if (ELF64_ST_BIND(sym->st_info) == STB_LOCAL) {
				def = sym;
			} else {
				def = lookup(name, type == R_X86_64_COPY ? d : 0, &dd);
				if (!def && ELF64_ST_BIND(sym->st_info) != STB_WEAK)
					fatal("symbol not found: ", name, d == &app ? "" : " (in libc.so)");
			}
		}
		uintptr_t value = def ? dd->base + def->st_value : 0;
		if (def && ELF64_ST_TYPE(def->st_info) == STT_GNU_IFUNC)
			fatal("IFUNC symbols are not supported: ", d->strings + sym->st_name, 0);
		switch (type) {
		case R_X86_64_64:
			*where = value + r[i].r_addend;
			break;
		case R_X86_64_GLOB_DAT:
		case R_X86_64_JUMP_SLOT:
			*where = value;
			break;
		case R_X86_64_COPY:
			if (!def)
				fatal("copy relocation of a missing symbol: ", d->strings + sym->st_name, 0);
			memcpy(where, (const void *)value, sym->st_size);
			break;
		case R_X86_64_TPOFF64:
			/* variant II: the module's block is at tp - tls_offset */
			*where = (def ? def->st_value : 0) + r[i].r_addend - dd->tls_offset;
			break;
		case R_X86_64_DTPMOD64:
			*where = def ? (uintptr_t)dd->tls_id : (uintptr_t)d->tls_id;
			break;
		case R_X86_64_DTPOFF64:
			*where = (def ? def->st_value : 0) + r[i].r_addend;
			break;
		case R_X86_64_IRELATIVE:
			fatal("IFUNC relocations are not supported in ", d->name, 0);
		default:
			fatal("unsupported relocation type in ", d->name, 0);
		}
	}
}

static void relocate(struct dso *d, int self_done)
{
	if (!self_done) {
		/* RELR holds only relative relocations */
		const Elf64_Relr *r = d->relr;
		uintptr_t *where = 0;
		for (size_t i = 0; r && i < d->relrsz / sizeof *r; i++) {
			Elf64_Relr e = r[i];
			if ((e & 1) == 0) {
				where = (uintptr_t *)(d->base + e);
				*where++ += d->base;
			} else {
				for (int j = 0; (e >>= 1) != 0; j++)
					if (e & 1)
						where[j] += d->base;
				where += 63;
			}
		}
	}
	if (d->rela)
		relocate_table(d, d->rela, d->relasz, self_done);
	if (d->jmprel)
		relocate_table(d, d->jmprel, d->jmprelsz, self_done);
}

static void protect_relro(const struct dso *d)
{
	uintptr_t s = ROUND_DOWN(d->relro_start, PAGE_SZ), e = ROUND_DOWN(d->relro_end, PAGE_SZ);
	if (e > s && __syscall3(SYS_mprotect, s, e - s, PROT_READ) < 0)
		fatal("cannot protect relocated data of ", d->name, 0);
}

/* ---- dependencies ---- */

static void check_needed(const struct dso *d)
{
	for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++) {
		if (v->d_tag != DT_NEEDED)
			continue;
		const char *n = d->strings + v->d_un.d_val;
		if (strcmp(n, "libc.so"))
			fatal("cannot load ", n, ": shared libraries other than libc.so are not supported yet");
	}
}

/* ---- constructors and destructors ---- */

static void run_init(const struct dso *d)
{
	for (size_t i = 0; i < d->preinit_n; i++)
		d->preinit_array[i]();
	if (d->init)
		((void (*)(void))d->init)();
	for (size_t i = 0; i < d->init_n; i++)
		d->init_array[i]();
}

/* exit(): destructors of the program first, then of libc (the reverse
 * of construction). */
hidden void __dl_fini(void)
{
	const struct dso *order[2] = { &app, &self };
	for (int k = 0; k < 2; k++) {
		const struct dso *d = order[k];
		for (size_t i = d->fini_n; i-- > 0;)
			d->fini_array[i]();
		if (d->fini)
			((void (*)(void))d->fini)();
	}
}

/* ---- the entry point: called by _dlstart with the initial stack ---- */

hidden uintptr_t __dls_start(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	size_t aux[AUX_CNT];
	__auxv_of(envp, aux);

	/* 1. ourselves: relative relocations only, before anything else */
	uintptr_t base = aux[AT_BASE];
	if (!base)
		fatal("this is the C library; it cannot be run as a program yet", 0, 0);
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
	self.base = base;
	self.phdr = (const Elf64_Phdr *)(base + eh->e_phoff);
	self.phnum = eh->e_phnum;
	for (size_t i = 0; i < self.phnum; i++)
		if (self.phdr[i].p_type == PT_DYNAMIC)
			__self_relocate(base, (const Elf64_Dyn *)(base + self.phdr[i].p_vaddr), 0);

	/* 2. the program */
	app.phdr = (const Elf64_Phdr *)aux[AT_PHDR];
	app.phnum = aux[AT_PHNUM];
	app.base = 0;
	for (size_t i = 0; i < app.phnum; i++) {
		if (app.phdr[i].p_type == PT_PHDR)
			app.base = aux[AT_PHDR] - app.phdr[i].p_vaddr;
	}
	app.name = argc > 0 && argv[0] ? argv[0] : "";
	self.name = "libc.so";
	for (size_t i = 0; i < app.phnum; i++)
		if (app.phdr[i].p_type == PT_INTERP)
			self.name = (const char *)(app.base + app.phdr[i].p_vaddr);

	/* 3. static TLS for both, then the thread pointer, canary and guard */
	const struct dso *mods[2] = { &app, &self };
	for (int k = 0; k < 2; k++) {
		struct dso *d = (struct dso *)mods[k];
		for (size_t i = 0; i < d->phnum; i++) {
			const Elf64_Phdr *p = &d->phdr[i];
			if (p->p_type == PT_TLS && p->p_memsz)
				d->tls_id = __tls_add((const void *)(d->base + p->p_vaddr), p->p_filesz, p->p_memsz, p->p_align);
		}
	}
	__tls_layout();
	for (int k = 0; k < 2; k++) {
		struct dso *d = (struct dso *)mods[k];
		if (d->tls_id)
			d->tls_offset = __libc.tls_mods[d->tls_id - 1].offset;
	}
	static const unsigned char zero_rnd[16];
	__setup_tcb(aux[AT_RANDOM] ? (const unsigned char *)aux[AT_RANDOM] : zero_rnd);
	__wipe_random(aux);

	/* 4. bind everything: libc first (its data must be final before the
	 * program's COPY relocations copy it), then the program */
	decode(&app);
	decode(&self);
	app.next = &self;
	__dso_head = &app;
	check_needed(&app);
	check_needed(&self);
	relocate(&self, 1);
	relocate(&app, 0);
	protect_relro(&self);
	protect_relro(&app);

	/* 5. libc's state, then constructors in dependency order */
	__libc.dynamic = 1;
	__init_libc(argc, argv, envp, aux);
	run_init(&self);
	run_init(&app);
	return aux[AT_ENTRY];
}

/* ---- for dl_iterate_phdr and _dl_find_object ---- */

hidden int __dl_object(int i, struct dl_phdr_info *info)
{
	struct dso *d = __dso_head;
	for (; d && i; i--)
		d = d->next;
	if (!d)
		return 0;
	*info = (struct dl_phdr_info){ 0 };
	info->dlpi_addr = d->base;
	info->dlpi_name = d == &app ? "" : d->name;
	info->dlpi_phdr = d->phdr;
	info->dlpi_phnum = (Elf64_Half)d->phnum;
	info->dlpi_adds = 2;
	if (d->tls_id) {
		info->dlpi_tls_modid = (size_t)d->tls_id;
		info->dlpi_tls_data = (char *)__self() - d->tls_offset;
	}
	return 1;
}

#endif
