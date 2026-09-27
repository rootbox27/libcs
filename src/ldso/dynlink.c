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
 * Libraries (DT_NEEDED) are searched in LD_LIBRARY_PATH, the requesting
 * object's DT_RPATH/DT_RUNPATH ($ORIGIN allowed), then the directory
 * libc.so is in. Setuid/setgid programs ignore LD_LIBRARY_PATH and
 * $ORIGIN. Symbols are searched in the program, then the libraries in
 * breadth-first load order; constructors run dependencies first.
 * Libraries' static TLS goes in space reserved below the main thread's
 * blocks, so the thread pointer never moves.
 *
 * Not yet: dlopen (only dlopen(NULL) works), debugger support.
 *
 * Built only into libc.so (CITADEL_SHARED), without the stack protector:
 * __dls_start runs before the thread pointer exists. */
#ifdef CITADEL_SHARED
#include "internal.h"
#include "ldso/dynlink.h"
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DSO 128
#define MAX_DEPS 1024
/* static TLS left free for libraries (address space; see __setup_tcb) */
#define TLS_SURPLUS (1 << 20)

static struct dso app, self;
hidden struct dso *__dso_head;
static struct dso *dso_tail;
static int dso_count;

/* The loader cannot use malloc (it needs environ, which is only set once
 * the program is relocated), so its memory is static. */
static struct dso pool[MAX_DSO];
static int pool_used;
static struct dso *deps_pool[MAX_DEPS];
static int deps_used;
static char names[32768];
static size_t names_used;

static int secure;
static const char *env_library_path;

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
					fatal("symbol not found: ", name, 0);
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

/* ---- loading libraries ---- */

static char *save(const char *a, size_t al, const char *b, size_t bl)
{
	if (names_used + al + bl + 1 > sizeof names)
		fatal("too many library names", 0, 0);
	char *p = names + names_used;
	memcpy(p, a, al);
	memcpy(p + al, b, bl);
	p[al + bl] = 0;
	names_used += al + bl + 1;
	return p;
}

static void append(struct dso *d)
{
	if (dso_tail)
		dso_tail->next = d;
	else
		__dso_head = d;
	dso_tail = d;
	d->next = 0;
	dso_count++;
}

/* Map the ELF shared object open on fd into d. */
static void map_library(int fd, struct dso *d)
{
	Elf64_Ehdr eh;
	if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, ELFMAG, SELFMAG) ||
	    eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_X86_64 || eh.e_type != ET_DYN ||
	    eh.e_phentsize != sizeof(Elf64_Phdr) || !eh.e_phnum || eh.e_phnum > 64)
		fatal("not an x86-64 shared library: ", d->name, 0);
	Elf64_Phdr ph[64];
	size_t phsz = eh.e_phnum * sizeof *ph;
	if (pread(fd, ph, phsz, (off_t)eh.e_phoff) != (ssize_t)phsz)
		fatal("cannot read ", d->name, 0);

	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if ((ph[i].p_flags & PF_W) && (ph[i].p_flags & PF_X))
			fatal("writable and executable segment in ", d->name, 0);
		if (ph[i].p_filesz > ph[i].p_memsz || (ph[i].p_vaddr - ph[i].p_offset) % PAGE_SZ)
			fatal("malformed segment in ", d->name, 0);
		if (ph[i].p_vaddr < lo)
			lo = ph[i].p_vaddr;
		if (ph[i].p_vaddr + ph[i].p_memsz > hi)
			hi = ph[i].p_vaddr + ph[i].p_memsz;
	}
	if (lo >= hi)
		fatal("nothing to load in ", d->name, 0);
	lo = ROUND_DOWN(lo, PAGE_SZ);
	hi = ROUND_UP(hi, PAGE_SZ);
	/* reserve the whole range; gaps between segments stay inaccessible */
	void *r = mmap(0, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED)
		fatal("out of address space loading ", d->name, 0);
	uintptr_t base = (uintptr_t)r - lo;
	for (int i = 0; i < eh.e_phnum; i++) {
		const Elf64_Phdr *p = &ph[i];
		if (p->p_type != PT_LOAD)
			continue;
		int prot = (p->p_flags & PF_R ? PROT_READ : 0) | (p->p_flags & PF_W ? PROT_WRITE : 0) |
		           (p->p_flags & PF_X ? PROT_EXEC : 0);
		uintptr_t va = base + ROUND_DOWN(p->p_vaddr, PAGE_SZ);
		off_t off = (off_t)ROUND_DOWN(p->p_offset, PAGE_SZ);
		uintptr_t file_end = base + p->p_vaddr + p->p_filesz;
		uintptr_t mem_end = base + p->p_vaddr + p->p_memsz;
		if (p->p_filesz && mmap((void *)va, file_end - va, prot, MAP_PRIVATE | MAP_FIXED, fd, off) == MAP_FAILED)
			fatal("cannot map ", d->name, 0);
		if (mem_end > file_end) {
			/* .bss: the rest of the last file page, then fresh pages */
			if (!(prot & PROT_WRITE))
				fatal("read-only .bss in ", d->name, 0);
			uintptr_t page_end = ROUND_UP(file_end, PAGE_SZ);
			if (p->p_filesz)
				memset((void *)file_end, 0, (page_end < mem_end ? page_end : mem_end) - file_end);
			else
				page_end = va;
			if (mem_end > page_end &&
			    mmap((void *)page_end, ROUND_UP(mem_end, PAGE_SZ) - page_end, prot,
			         MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
				fatal("cannot map ", d->name, 0);
		}
	}
	d->base = base;
	d->map = r;
	d->map_len = hi - lo;
	/* the program headers, as mapped (they are in the first segment) */
	d->phdr = 0;
	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == PT_LOAD && eh.e_phoff >= ph[i].p_offset &&
		    eh.e_phoff + phsz <= ph[i].p_offset + ph[i].p_filesz) {
			d->phdr = (const Elf64_Phdr *)(base + ph[i].p_vaddr + (eh.e_phoff - ph[i].p_offset));
			break;
		}
	}
	if (!d->phdr)
		fatal("program headers not loaded in ", d->name, 0);
	d->phnum = eh.e_phnum;
}

/* Try one directory (length dl) for name; returns an open fd or -1. */
static int try_dir(const char *dir, size_t dl, const char *name, const char *origin, size_t ol, char *path,
                   size_t cap)
{
	size_t n = 0;
	/* expand $ORIGIN (or ${ORIGIN}) */
	for (size_t i = 0; i < dl;) {
		const char *rest = dir + i;
		size_t skip = 0;
		if (dl - i >= 7 && !memcmp(rest, "$ORIGIN", 7))
			skip = 7;
		else if (dl - i >= 9 && !memcmp(rest, "${ORIGIN}", 9))
			skip = 9;
		if (skip) {
			if (secure || !origin)
				return -1; /* setuid: never relative to where the file is */
			if (n + ol >= cap)
				return -1;
			memcpy(path + n, origin, ol);
			n += ol;
			i += skip;
		} else {
			if (n + 1 >= cap)
				return -1;
			path[n++] = dir[i++];
		}
	}
	size_t l = strlen(name);
	if (!n || n + 1 + l + 1 > cap)
		return -1;
	path[n++] = '/';
	memcpy(path + n, name, l + 1);
	return open(path, O_RDONLY | O_CLOEXEC);
}

/* Search a colon-separated list of directories. */
static int search(const char *list, const char *name, const char *origin, size_t ol, char *path, size_t cap)
{
	while (list && *list) {
		size_t dl = strcspn(list, ":");
		if (dl) {
			int fd = try_dir(list, dl, name, origin, ol, path, cap);
			if (fd >= 0)
				return fd;
		}
		list += dl;
		if (*list == ':')
			list++;
	}
	return -1;
}

static const char *dyn_string(const struct dso *d, long tag)
{
	for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++)
		if (v->d_tag == tag)
			return d->strings + v->d_un.d_val;
	return 0;
}

static size_t dir_len(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? (size_t)(slash - path) : 0;
}

static struct dso *load_library(const char *name, struct dso *needer)
{
	/* libc.so is us */
	const char *base = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
	if (!strcmp(base, "libc.so")) {
		if (!self.in_list)
			self.in_list = 1, append(&self);
		return &self;
	}
	for (struct dso *d = __dso_head; d; d = d->next)
		if (d->shortname && !strcmp(d->shortname, name))
			return d;

	char path[4096];
	int fd = -1;
	if (strchr(name, '/')) {
		size_t l = strlen(name);
		if (l >= sizeof path)
			fatal("library path too long: ", name, 0);
		memcpy(path, name, l + 1);
		fd = open(path, O_RDONLY | O_CLOEXEC);
	} else {
		const char *origin = needer->name;
		size_t ol = dir_len(origin);
		const char *runpath = dyn_string(needer, DT_RUNPATH);
		const char *rpath = runpath ? 0 : dyn_string(needer, DT_RPATH);
		if (!secure && env_library_path)
			fd = search(env_library_path, name, origin, ol, path, sizeof path);
		if (fd < 0 && rpath)
			fd = search(rpath, name, origin, ol, path, sizeof path);
		if (fd < 0 && runpath)
			fd = search(runpath, name, origin, ol, path, sizeof path);
		if (fd < 0) {
			/* the directory libc.so is in */
			size_t sl = dir_len(self.name);
			fd = try_dir(self.name, sl ? sl : 1, name, 0, 0, path, sizeof path);
		}
	}
	if (fd < 0)
		fatal("cannot find library ", name, needer == &app ? "" : " (needed by a library)");

	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
		fatal("not a regular file: ", path, 0);
	/* the same file under another name is the same library */
	for (struct dso *d = __dso_head; d; d = d->next) {
		if (d->dev == st.st_dev && d->ino == st.st_ino && d != &app) {
			close(fd);
			return d;
		}
	}
	if (pool_used == MAX_DSO)
		fatal("too many libraries", 0, 0);
	struct dso *d = &pool[pool_used++];
	d->name = save(path, strlen(path), "", 0);
	d->shortname = save(name, strlen(name), "", 0);
	d->dev = st.st_dev;
	d->ino = st.st_ino;
	map_library(fd, d);
	close(fd);
	decode(d);
	d->in_list = 1;
	append(d);
	return d;
}

/* Load every dependency, breadth first; the list order is the symbol
 * search order. Each object's dependencies are recorded for the
 * constructor order. */
static void load_deps(void)
{
	for (struct dso *d = __dso_head; d; d = d->next) {
		size_t n = 0;
		for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++)
			n += v->d_tag == DT_NEEDED;
		if (deps_used + n > MAX_DEPS)
			fatal("too many dependencies", 0, 0);
		d->deps = &deps_pool[deps_used];
		deps_used += (int)n;
		for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++)
			if (v->d_tag == DT_NEEDED)
				d->deps[d->ndeps++] = load_library(d->strings + v->d_un.d_val, d);
	}
	if (!self.in_list)
		self.in_list = 1, append(&self);
}

/* Static TLS for the libraries, below the program's and libc's. */
static void setup_library_tls(size_t limit)
{
	for (struct dso *d = __dso_head; d; d = d->next) {
		if (d == &app || d == &self)
			continue;
		for (size_t i = 0; i < d->phnum; i++) {
			const Elf64_Phdr *p = &d->phdr[i];
			if (p->p_type == PT_TLS && p->p_memsz)
				d->tls_id = __tls_add((const void *)(d->base + p->p_vaddr), p->p_filesz, p->p_memsz,
				                      p->p_align);
		}
	}
	__tls_layout();
	if (__libc.tls_offset > limit)
		fatal("the libraries' thread-local storage is too large", 0, 0);
	uintptr_t tp = (uintptr_t)__self();
	for (struct dso *d = __dso_head; d; d = d->next) {
		if (!d->tls_id)
			continue;
		const struct tls_mod *m = &__libc.tls_mods[d->tls_id - 1];
		d->tls_offset = m->offset;
		if (d != &app && d != &self)
			memcpy((void *)(tp - m->offset), m->image, m->filesz);
	}
}

/* __tls_get_addr: general-dynamic TLS access. Every module loaded at
 * startup has static TLS, so this is plain arithmetic. */
typedef struct {
	unsigned long ti_module, ti_offset;
} tls_index;

void *__tls_get_addr(tls_index *ti)
{
	return (char *)__self() - __libc.tls_mods[ti->ti_module - 1].offset + ti->ti_offset;
}

/* ---- constructors and destructors ---- */

static struct dso *init_order[MAX_DSO + 2];
static int init_n;

/* dependencies before the objects that need them (depth-first
 * post-order from the program) */
static void order_init(struct dso *d)
{
	if (d->visited)
		return;
	d->visited = 1;
	for (int i = 0; i < d->ndeps; i++)
		order_init(d->deps[i]);
	init_order[init_n++] = d;
}

static void run_init(const struct dso *d)
{
	for (size_t i = 0; i < d->preinit_n; i++)
		d->preinit_array[i]();
	if (d->init)
		((void (*)(void))d->init)();
	for (size_t i = 0; i < d->init_n; i++)
		d->init_array[i]();
}

/* exit(): destructors in the reverse order of the constructors */
hidden void __dl_fini(void)
{
	for (int k = init_n; k-- > 0;) {
		const struct dso *d = init_order[k];
		for (size_t i = d->fini_n; i-- > 0;)
			d->fini_array[i]();
		if (d->fini)
			((void (*)(void))d->fini)();
	}
}

/* ---- the entry point: called by _dlstart with the initial stack ----
 *
 * _dlstart first calls __dls_relocate_self, then __dls_start. They must
 * be separate calls: the compiler may read any relocated constant (an
 * address in .data.rel.ro) at the start of a function, so nothing that
 * runs before the relative relocations are applied may share a function
 * with code that uses them. */

hidden __attribute__((__noinline__)) void __dls_relocate_self(long *sp)
{
	int argc = (int)sp[0];
	char **envp = (char **)(sp + 1) + argc + 1;
	size_t aux[AUX_CNT];
	__auxv_of(envp, aux);
	uintptr_t base = aux[AT_BASE];
	if (!base) {
		/* run as a program: nothing is relocated, so no messages */
		__syscall1(SYS_exit_group, 127);
		for (;;) ;
	}
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + eh->e_phoff);
	for (size_t i = 0; i < eh->e_phnum; i++)
		if (ph[i].p_type == PT_DYNAMIC)
			__self_relocate(base, (const Elf64_Dyn *)(base + ph[i].p_vaddr), 0);
}

hidden uintptr_t __dls_start(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	size_t aux[AUX_CNT];
	__auxv_of(envp, aux);

	/* 1. ourselves: __dls_relocate_self has applied the relative
	 * relocations */
	uintptr_t base = aux[AT_BASE];
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
	self.base = base;
	self.phdr = (const Elf64_Phdr *)(base + eh->e_phoff);
	self.phnum = eh->e_phnum;

	/* 2. the program */
	app.phdr = (const Elf64_Phdr *)aux[AT_PHDR];
	app.phnum = aux[AT_PHNUM];
	app.base = 0;
	for (size_t i = 0; i < app.phnum; i++)
		if (app.phdr[i].p_type == PT_PHDR)
			app.base = aux[AT_PHDR] - app.phdr[i].p_vaddr;
	/* its path, for $ORIGIN */
	app.name = aux[AT_EXECFN] ? (const char *)aux[AT_EXECFN] : argc > 0 && argv[0] ? argv[0] : "";
	self.name = "libc.so";
	for (size_t i = 0; i < app.phnum; i++)
		if (app.phdr[i].p_type == PT_INTERP)
			self.name = (const char *)(app.base + app.phdr[i].p_vaddr);

	/* 3. static TLS of the program and libc, with room left below for
	 * the libraries; then the thread pointer, canary and guard */
	struct dso *mods[2] = { &app, &self };
	for (int k = 0; k < 2; k++) {
		struct dso *d = mods[k];
		for (size_t i = 0; i < d->phnum; i++) {
			const Elf64_Phdr *p = &d->phdr[i];
			if (p->p_type == PT_TLS && p->p_memsz)
				d->tls_id = __tls_add((const void *)(d->base + p->p_vaddr), p->p_filesz, p->p_memsz, p->p_align);
		}
	}
	__tls_layout();
	size_t tls_limit = __libc.tls_offset + TLS_SURPLUS;
	for (int k = 0; k < 2; k++)
		if (mods[k]->tls_id)
			mods[k]->tls_offset = __libc.tls_mods[mods[k]->tls_id - 1].offset;
	static const unsigned char zero_rnd[16];
	__setup_tcb(aux[AT_RANDOM] ? (const unsigned char *)aux[AT_RANDOM] : zero_rnd, TLS_SURPLUS);
	__wipe_random(aux);

	/* 4. bind libc against the program and itself, so the loader can use
	 * libc's functions from here on */
	decode(&app);
	decode(&self);
	app.next = &self;
	__dso_head = &app;
	relocate(&self, 1);

	/* 5. the libraries */
	secure = aux[AT_SECURE] != 0 || aux[AT_UID] != aux[AT_EUID] || aux[AT_GID] != aux[AT_EGID];
	for (char **e = envp; *e; e++)
		if (!strncmp(*e, "LD_LIBRARY_PATH=", 16))
			env_library_path = *e + 16;
	app.next = 0;
	__dso_head = 0;
	dso_tail = 0;
	dso_count = 0;
	app.in_list = 1;
	append(&app);
	load_deps();
	setup_library_tls(tls_limit);

	/* 6. bind everything else: dependencies before the objects that
	 * need them (the program last: its COPY relocations copy their
	 * data), then make the relocated data read-only */
	order_init(&app);
	for (int k = 0; k < init_n; k++)
		if (init_order[k] != &self)
			relocate(init_order[k], 0);
	for (struct dso *d = __dso_head; d; d = d->next)
		protect_relro(d);

	/* 7. libc's state, then the constructors */
	__libc.dynamic = 1;
	__init_libc(argc, argv, envp, aux);
	for (int k = 0; k < init_n; k++)
		run_init(init_order[k]);
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
	info->dlpi_adds = (unsigned long long)dso_count;
	if (d->tls_id) {
		info->dlpi_tls_modid = (size_t)d->tls_id;
		info->dlpi_tls_data = (char *)__self() - d->tls_offset;
	}
	return 1;
}

#endif
