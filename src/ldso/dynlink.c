/* The dynamic linker. libc.so is its own dynamic linker (programs name it
 * as their interpreter), so libc and the loader share one copy of their
 * state: TLS, malloc, errno.
 *
 * Policy:
 *  - every symbol is bound when its object is loaded (no lazy binding),
 *    and the relocated data (GOTs included) is then made read-only;
 *  - text relocations and IFUNCs are refused;
 *  - no symbol versioning: programs are built against this libc.
 *
 * Libraries (DT_NEEDED) are searched in LD_LIBRARY_PATH, the requesting
 * object's DT_RPATH/DT_RUNPATH ($ORIGIN allowed), then the directory
 * libc.so is in. Setuid/setgid programs ignore LD_LIBRARY_PATH,
 * LD_PRELOAD and $ORIGIN. Symbols are searched in the global scope (the
 * program, then the libraries in breadth-first load order, then
 * RTLD_GLOBAL objects), then, for a dlopen'd object, the closure it was
 * loaded with. Constructors run dependencies first.
 *
 * Thread-local storage is all static: every module's block lies at a
 * fixed offset below each thread's pointer. The main thread reserves
 * space for the libraries loaded at startup and every thread reserves
 * DLOPEN_TLS more for dlopen, so __tls_get_addr and TLS descriptors are
 * plain arithmetic, and a library's initial-exec TLS works too.
 *
 * dlclose unloads the objects no handle needs any more, unless they have
 * TLS, are marked NODELETE, registered a thread_local destructor or had
 * symbols bound by an object outside their own dependency closure: those
 * stay (their memory could otherwise still be referenced).
 *
 * Debuggers find the object list through DT_DEBUG and _r_debug, and stop
 * in _dl_debug_state whenever it changes.
 *
 * Run as a program, libc.so loads and runs the program named by its first
 * argument (libc.so [--list] program [args]); --list, or being run as
 * "ldd", prints the libraries the program would load instead.
 *
 * Built only into libc.so (CITADEL_SHARED), without the stack protector:
 * __dls_start runs before the thread pointer exists. */
#ifdef CITADEL_SHARED
#include "internal.h"
#include "ldso/dynlink.h"
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* the loader's memory before malloc can be used */
#define STARTUP_ARENA (128 << 10)
/* static TLS reserved in the main thread for startup libraries, and in
 * every thread for dlopen (address space: untouched pages cost nothing) */
#define TLS_SURPLUS (1 << 20)
#define DLOPEN_TLS (256 << 10)

extern const Elf64_Ehdr __ehdr_start __attribute__((__visibility__("hidden")));
hidden void __tlsdesc_static(void);

static struct dso app, self;
static struct dso *head, *tail;
static int self_listed;
static unsigned long long adds, subs;
static struct dso *fini_head;
static int started;        /* startup is over: allocate with malloc, fail softly */
static unsigned mark_gen;

static int secure;
static const char *env_library_path, *env_preload;

/* ---- the debugger interface ---- */

struct r_debug _r_debug;

__attribute__((__noinline__, __used__)) void _dl_debug_state(void)
{
	__asm__ __volatile__("" ::: "memory");
}

static void debug_event(int state)
{
	_r_debug.r_state = state;
	_dl_debug_state();
}

/* ---- the lock: recursive, as constructors may call dlopen ---- */

static volatile int lock_word;
static int lock_owner, lock_depth;

static void dl_lock(void)
{
	int tid = __self()->tid;
	if (__atomic_load_n(&lock_owner, __ATOMIC_RELAXED) == tid) {
		lock_depth++;
		return;
	}
	__lock(&lock_word);
	__atomic_store_n(&lock_owner, tid, __ATOMIC_RELAXED);
	lock_depth = 1;
}

static void dl_unlock(void)
{
	if (--lock_depth)
		return;
	__atomic_store_n(&lock_owner, 0, __ATOMIC_RELAXED);
	__unlock(&lock_word);
}

/* fork: taken around it; the child is the one thread holding it */
hidden void __dl_atfork(int phase)
{
	if (phase < 0) {
		dl_lock();
		return;
	}
	if (phase > 0)
		__atomic_store_n(&lock_owner, __self()->tid, __ATOMIC_RELAXED);
	dl_unlock();
}

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
	if (b)
		say(b);
	if (c)
		say(c);
	say("\n");
	__syscall1(SYS_exit_group, 127);
	for (;;) ;
}

/* Errors: fatal at startup; in dlopen, the message is kept for dlerror
 * and dlopen undoes everything it did. */
static jmp_buf *fail_jb;
static char fail_msg[512];
static int cur_fd = -1;

static __attribute__((__noreturn__)) void fail(const char *a, const char *b, const char *c)
{
	if (!fail_jb)
		fatal(a, b, c);
	const char *parts[3] = { a, b, c };
	size_t n = 0;
	for (int i = 0; i < 3; i++)
		for (const char *p = parts[i]; p && *p && n < sizeof fail_msg - 1; p++)
			fail_msg[n++] = *p;
	fail_msg[n] = 0;
	longjmp(*fail_jb, 1);
}

static _Thread_local char err_buf[256];
static _Thread_local int err_set;

static void set_error(const char *a, const char *b)
{
	size_t n = 0;
	for (const char *p = a; *p && n < sizeof err_buf - 1; p++)
		err_buf[n++] = *p;
	for (const char *p = b; p && *p && n < sizeof err_buf - 1; p++)
		err_buf[n++] = *p;
	err_buf[n] = 0;
	err_set = 1;
}

/* ---- memory ---- */

static _Alignas(16) char arena[STARTUP_ARENA];
static size_t arena_used;

/* zeroed memory */
static void *dl_alloc(size_t n)
{
	if (!started) {
		n = ROUND_UP(n, 16);
		if (n > sizeof arena - arena_used)
			fatal("out of memory", 0, 0);
		void *p = arena + arena_used;
		arena_used += n;
		return p;
	}
	void *p = calloc(1, n ? n : 1);
	if (!p)
		fail("out of memory", 0, 0);
	return p;
}

static char *dl_strdup(const char *s, size_t l)
{
	char *p = dl_alloc(l + 1);
	memcpy(p, s, l);
	p[l] = 0;
	return p;
}

static void free_dso(struct dso *d)
{
	if (!d->allocated)
		return;
	free(d->name);
	free((char *)d->shortname);
	free(d->deps);
	free(d->scope);
	free(d);
}

/* ---- the object list ---- */

static void append(struct dso *d)
{
	d->prev = tail;
	d->next = 0;
	if (tail)
		tail->next = d;
	else
		head = d;
	tail = d;
	adds++;
}

static void unlink_dso(struct dso *d)
{
	if (d->prev)
		d->prev->next = d->next;
	else
		head = d->next;
	if (d->next)
		d->next->prev = d->prev;
	else
		tail = d->prev;
	subs++;
}

static void set_extent(struct dso *d)
{
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (size_t i = 0; i < d->phnum; i++) {
		const Elf64_Phdr *p = &d->phdr[i];
		if (p->p_type != PT_LOAD)
			continue;
		if (p->p_vaddr < lo)
			lo = p->p_vaddr;
		if (p->p_vaddr + p->p_memsz > hi)
			hi = p->p_vaddr + p->p_memsz;
	}
	if (lo < hi) {
		d->map = (void *)(d->base + ROUND_DOWN(lo, PAGE_SZ));
		d->map_len = ROUND_UP(hi, PAGE_SZ) - ROUND_DOWN(lo, PAGE_SZ);
	}
}

/* the object whose address range holds addr (not inlined: GCC takes
 * its loop for a variable live across dlopen's setjmp) */
static __attribute__((__noinline__)) struct dso *addr_dso(uintptr_t addr)
{
	for (struct dso *d = head; d; d = d->next)
		if (addr - (uintptr_t)d->map < d->map_len)
			return d;
	return 0;
}

static struct dso *valid_handle(void *h)
{
	for (struct dso *d = head; d; d = d->next)
		if (d == h)
			return d;
	return 0;
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
		fail("no dynamic section in ", d->path, 0);
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
		case DT_FLAGS_1: d->flags_1 = v->d_un.d_val; break;
		case DT_TEXTREL:
			fail("text relocations are not supported: ", d->path, 0);
		case DT_FLAGS:
			if (v->d_un.d_val & DF_TEXTREL)
				fail("text relocations are not supported: ", d->path, 0);
			break;
		}
	}
	if (!d->syms || !d->strings || (!d->hashtab && !d->ghashtab))
		fail("incomplete dynamic section in ", d->path, 0);
	if (d->flags_1 & DF_1_NODELETE)
		d->nodelete = 1;
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
	        type == STT_TLS || type == STT_GNU_IFUNC);
}

struct name {
	const char *s;
	uint32_t gh, sh;
};

static struct name mkname(const char *s)
{
	return (struct name){ s, gnu_hash(s), sysv_hash(s) };
}

static const Elf64_Sym *lookup_in(const struct dso *d, const struct name *n)
{
	if (d->ghashtab) {
		const uint32_t *ht = d->ghashtab;
		uint32_t nbuckets = ht[0], symoffset = ht[1], bloom_size = ht[2], shift = ht[3];
		const uint64_t *bloom = (const uint64_t *)(ht + 4);
		uint64_t word = bloom[(n->gh / 64) % bloom_size];
		uint64_t mask = (1ul << (n->gh % 64)) | (1ul << ((n->gh >> shift) % 64));
		if ((word & mask) != mask)
			return 0;
		const uint32_t *buckets = (const uint32_t *)(bloom + bloom_size);
		const uint32_t *chain = buckets + nbuckets;
		uint32_t i = buckets[n->gh % nbuckets];
		if (!i)
			return 0;
		for (;; i++) {
			uint32_t h2 = chain[i - symoffset];
			const Elf64_Sym *s = &d->syms[i];
			if ((n->gh | 1) == (h2 | 1) && defined(s) && !strcmp(n->s, d->strings + s->st_name))
				return s;
			if (h2 & 1)
				return 0;
		}
	}
	const uint32_t *ht = d->hashtab;
	uint32_t nbucket = ht[0];
	const uint32_t *bucket = ht + 2, *chain = bucket + nbucket;
	for (uint32_t i = bucket[n->sh % nbucket]; i; i = chain[i]) {
		const Elf64_Sym *s = &d->syms[i];
		if (defined(s) && !strcmp(n->s, d->strings + s->st_name))
			return s;
	}
	return 0;
}

static const Elf64_Sym *lookup_scope(struct dso *const *scope, int n, const struct name *nm, const struct dso *skip,
                                     struct dso **where)
{
	for (int i = 0; i < n; i++) {
		if (scope[i] == skip)
			continue;
		const Elf64_Sym *s = lookup_in(scope[i], nm);
		if (s) {
			*where = scope[i];
			return s;
		}
	}
	return 0;
}

/* The global scope, then `local` (the closure an object was loaded with),
 * skipping `skip` (for COPY relocations). */
static const Elf64_Sym *lookup(const struct name *nm, struct dso *const *local, int nlocal, const struct dso *skip,
                               struct dso **where)
{
	for (struct dso *d = head; d; d = d->next) {
		if (!d->global || d == skip)
			continue;
		const Elf64_Sym *s = lookup_in(d, nm);
		if (s) {
			*where = d;
			return s;
		}
	}
	return lookup_scope(local, nlocal, nm, skip, where);
}

/* ---- relocation ---- */

/* mark d's dependency closure with a fresh generation */
static void mark_closure(struct dso *d, unsigned gen)
{
	if (d->mark == gen)
		return;
	d->mark = gen;
	for (int i = 0; i < d->ndeps; i++)
		mark_closure(d->deps[i], gen);
}

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
				struct name nm = mkname(name);
				def = lookup(&nm, d->lscope, d->nlscope, type == R_X86_64_COPY ? d : 0, &dd);
				if (!def && ELF64_ST_BIND(sym->st_info) != STB_WEAK)
					fail("symbol not found: ", name, 0);
			}
			if (def && ELF64_ST_TYPE(def->st_info) == STT_GNU_IFUNC)
				fail("IFUNC symbols are not supported: ", name, 0);
			/* bound to an object this one does not depend on: it
			 * must outlive this one */
			if (def && dd != d && !dd->permanent && dd->mark != mark_gen)
				dd->nodelete = 1;
		}
		uintptr_t value = def ? dd->base + def->st_value : 0;
		/* TLS: an offset in the defining module's block */
		uintptr_t tlsval = (def ? def->st_value : 0) + r[i].r_addend;
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
				fail("copy relocation of a missing symbol: ", d->strings + sym->st_name, 0);
			memcpy(where, (const void *)value, sym->st_size);
			break;
		case R_X86_64_TPOFF64:
			/* variant II: the module's block is at tp - tls_offset */
			*where = tlsval - dd->tls_offset;
			break;
		case R_X86_64_DTPMOD64:
			*where = (uintptr_t)dd->tls_id;
			break;
		case R_X86_64_DTPOFF64:
			*where = tlsval;
			break;
		case R_X86_64_TLSDESC:
			/* every block is static: the descriptor holds the
			 * offset from the thread pointer */
			where[0] = (uintptr_t)__tlsdesc_static;
			where[1] = tlsval - dd->tls_offset;
			break;
		case R_X86_64_IRELATIVE:
			fail("IFUNC relocations are not supported in ", d->path, 0);
		default:
			fail("unsupported relocation type in ", d->path, 0);
		}
	}
}

static void relocate(struct dso *d, int self_done)
{
	mark_closure(d, ++mark_gen);
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
	d->relocated = 1;
}

static void protect_relro(const struct dso *d)
{
	uintptr_t s = ROUND_DOWN(d->relro_start, PAGE_SZ), e = ROUND_DOWN(d->relro_end, PAGE_SZ);
	if (e > s && __syscall3(SYS_mprotect, s, e - s, PROT_READ) < 0)
		fail("cannot protect relocated data of ", d->path, 0);
}

/* ---- loading ---- */

/* Map the ELF object open on fd into d: a shared library, or with exec a
 * program too (libc.so run directly). Returns its entry point. */
static uintptr_t map_library(int fd, struct dso *d, int exec)
{
	Elf64_Ehdr eh;
	if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.e_ident, ELFMAG, SELFMAG) ||
	    eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_machine != EM_X86_64 ||
	    !(eh.e_type == ET_DYN || (exec && eh.e_type == ET_EXEC)) || eh.e_phentsize != sizeof(Elf64_Phdr) ||
	    !eh.e_phnum || eh.e_phnum > 64)
		fail(exec ? "not an x86-64 program: " : "not an x86-64 shared library: ", d->path, 0);
	Elf64_Phdr ph[64];
	size_t phsz = eh.e_phnum * sizeof *ph;
	if (pread(fd, ph, phsz, (off_t)eh.e_phoff) != (ssize_t)phsz)
		fail("cannot read ", d->path, 0);

	uintptr_t lo = UINTPTR_MAX, hi = 0;
	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if ((ph[i].p_flags & PF_W) && (ph[i].p_flags & PF_X))
			fail("writable and executable segment in ", d->path, 0);
		if (ph[i].p_filesz > ph[i].p_memsz || (ph[i].p_vaddr - ph[i].p_offset) % PAGE_SZ)
			fail("malformed segment in ", d->path, 0);
		if (ph[i].p_vaddr < lo)
			lo = ph[i].p_vaddr;
		if (ph[i].p_vaddr + ph[i].p_memsz > hi)
			hi = ph[i].p_vaddr + ph[i].p_memsz;
	}
	if (lo >= hi)
		fail("nothing to load in ", d->path, 0);
	lo = ROUND_DOWN(lo, PAGE_SZ);
	hi = ROUND_UP(hi, PAGE_SZ);
	/* reserve the whole range; gaps between segments stay inaccessible.
	 * A non-PIE program goes where it was linked, if that is free. */
	int fixed = eh.e_type == ET_EXEC;
	void *r = mmap(fixed ? (void *)lo : 0, hi - lo, PROT_NONE,
	               MAP_PRIVATE | MAP_ANONYMOUS | (fixed ? MAP_FIXED_NOREPLACE : 0), -1, 0);
	if (r == MAP_FAILED || (fixed && r != (void *)lo)) {
		if (r != MAP_FAILED)
			munmap(r, hi - lo);
		fail("cannot reserve address space for ", d->path, 0);
	}
	d->map = r;
	d->map_len = hi - lo;
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
			fail("cannot map ", d->path, 0);
		if (mem_end > file_end) {
			/* .bss: the rest of the last file page, then fresh pages */
			if (!(prot & PROT_WRITE))
				fail("read-only .bss in ", d->path, 0);
			uintptr_t page_end = ROUND_UP(file_end, PAGE_SZ);
			if (p->p_filesz)
				memset((void *)file_end, 0, (page_end < mem_end ? page_end : mem_end) - file_end);
			else
				page_end = va;
			if (mem_end > page_end &&
			    mmap((void *)page_end, ROUND_UP(mem_end, PAGE_SZ) - page_end, prot,
			         MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED)
				fail("cannot map ", d->path, 0);
		}
	}
	d->base = base;
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
		fail("program headers not loaded in ", d->path, 0);
	d->phnum = eh.e_phnum;
	return base + eh.e_entry;
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

static void list_self(void)
{
	if (!self_listed) {
		self_listed = 1;
		append(&self);
	}
}

static struct dso *find_loaded(const char *name)
{
	const char *base = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
	if (!strcmp(base, "libc.so"))
		return &self;
	for (struct dso *d = head; d; d = d->next) {
		if (d->shortname && !strcmp(d->shortname, name))
			return d;
		const char *soname = d->dynv && d != &app ? dyn_string(d, DT_SONAME) : 0;
		if (soname && !strcmp(soname, name))
			return d;
	}
	return 0;
}

/* Load name for needer (whose paths are searched). `direct`: asked for
 * by the program or dlopen, not by a library. */
static struct dso *load_library(const char *name, struct dso *needer, int direct)
{
	struct dso *d = find_loaded(name);
	if (d == &self)
		list_self();
	if (d)
		return d;

	char path[4096];
	int fd = -1;
	if (strchr(name, '/')) {
		size_t l = strlen(name);
		if (l >= sizeof path)
			fail("library path too long: ", name, 0);
		memcpy(path, name, l + 1);
		fd = open(path, O_RDONLY | O_CLOEXEC);
	} else {
		const char *origin = needer->path;
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
			size_t sl = dir_len(self.path);
			fd = try_dir(self.path, sl ? sl : 1, name, 0, 0, path, sizeof path);
		}
	}
	if (fd < 0)
		fail("cannot find library ", name, direct ? "" : " (needed by a library)");
	cur_fd = fd;

	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode))
		fail("not a regular file: ", path, 0);
	/* the same file under another name is the same library */
	for (d = head; d; d = d->next) {
		if (d != &app && d != &self && d->dev == st.st_dev && d->ino == st.st_ino) {
			close(fd);
			cur_fd = -1;
			return d;
		}
	}
	d = dl_alloc(sizeof *d);
	d->allocated = (unsigned char)started;
	d->name = dl_strdup(path, strlen(path));
	d->path = d->name;
	d->shortname = dl_strdup(name, strlen(name));
	d->dev = st.st_dev;
	d->ino = st.st_ino;
	d->global = d->permanent = !started;
	append(d); /* first, so that a failure below unmaps it */
	map_library(fd, d, 0);
	close(fd);
	cur_fd = -1;
	decode(d);
	if (started && (d->flags_1 & DF_1_PIE))
		fail("cannot load a program as a library: ", path, 0);
	return d;
}

/* Load the dependencies of every object from `from` on, breadth first;
 * the list order is the symbol search order. The program's LD_PRELOAD
 * libraries come first among its dependencies. */
static void load_deps(struct dso *from)
{
	for (struct dso *d = from; d; d = d->next) {
		if (d->deps)
			continue;
		size_t n = 0, np = 0;
		for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++)
			n += v->d_tag == DT_NEEDED;
		const char *pre = d == &app && !secure ? env_preload : 0;
		for (const char *p = pre; p && *p; p++)
			np += *p != ' ' && *p != ':' && (p == pre || p[-1] == ' ' || p[-1] == ':');
		d->deps = dl_alloc((n + np + 1) * sizeof *d->deps);
		while (pre && *pre) {
			size_t l = strcspn(pre, " :");
			if (l) {
				char buf[4096];
				if (l >= sizeof buf)
					fail("LD_PRELOAD entry too long", 0, 0);
				memcpy(buf, pre, l);
				buf[l] = 0;
				d->deps[d->ndeps++] = load_library(buf, d, 1);
			}
			pre += l;
			if (*pre)
				pre++;
		}
		for (const Elf64_Dyn *v = d->dynv; v->d_tag; v++)
			if (v->d_tag == DT_NEEDED)
				d->deps[d->ndeps++] = load_library(d->strings + v->d_un.d_val, d, d == &app);
	}
}

/* A handle's dependency closure, breadth first. */
static void make_scope(struct dso *h)
{
	if (h->scope)
		return;
	int n = 0;
	for (struct dso *d = head; d; d = d->next)
		n++;
	struct dso **q = dl_alloc((size_t)n * sizeof *q);
	unsigned gen = ++mark_gen;
	int len = 1;
	q[0] = h;
	h->mark = gen;
	for (int i = 0; i < len; i++) {
		for (int k = 0; k < q[i]->ndeps; k++) {
			struct dso *dep = q[i]->deps[k];
			if (dep->mark != gen) {
				dep->mark = gen;
				q[len++] = dep;
			}
		}
	}
	h->scope = q;
	h->nscope = len;
}

/* ---- thread-local storage ---- */

/* Give each object from `from` on that has TLS a static block; `limit` is
 * the space every thread has. */
static int assign_tls(struct dso *from, size_t limit)
{
	int any = 0;
	for (struct dso *d = from; d; d = d->next) {
		if (d->tls_id || d == &app || d == &self)
			continue;
		for (size_t i = 0; i < d->phnum; i++) {
			const Elf64_Phdr *p = &d->phdr[i];
			if (p->p_type != PT_TLS || !p->p_memsz)
				continue;
			/* the thread pointers are aligned for the modules
			 * known when the threads were made */
			if (p->p_align > __libc.tls_align)
				fail("thread-local storage alignment too large in ", d->path, 0);
			if (__libc.tls_count >= TLS_MODS_MAX)
				fail("too many modules with thread-local storage", 0, 0);
			d->tls_id = __tls_add((const void *)(d->base + p->p_vaddr), p->p_filesz, p->p_memsz, p->p_align);
			any = 1;
		}
	}
	__tls_layout();
	if (__libc.tls_offset > limit)
		fail("out of static thread-local storage loading ", tail->path, 0);
	for (struct dso *d = from; d; d = d->next)
		if (d->tls_id)
			d->tls_offset = __libc.tls_mods[d->tls_id - 1].offset;
	return any;
}

/* (Re)initialise the blocks of the objects from `from` on in thread t. */
static void init_tls(struct pthread *t, void *arg)
{
	for (struct dso *d = arg; d; d = d->next) {
		if (!d->tls_id)
			continue;
		const struct tls_mod *m = &__libc.tls_mods[d->tls_id - 1];
		unsigned char *b = (unsigned char *)t - m->offset;
		memcpy(b, m->image, m->filesz);
		memset(b + m->filesz, 0, m->memsz - m->filesz);
	}
}

/* __tls_get_addr: general-dynamic TLS access. Every module has static
 * TLS, so this is plain arithmetic. */
typedef struct {
	unsigned long ti_module, ti_offset;
} tls_index;

void *__tls_get_addr(tls_index *ti)
{
	return (char *)__self() - __libc.tls_mods[ti->ti_module - 1].offset + ti->ti_offset;
}

/* ---- constructors and destructors ---- */

/* dependencies before the objects that need them (depth-first
 * post-order); only objects not yet visited */
static void order_init(struct dso *d, struct dso **out, int *n)
{
	if (d->visited)
		return;
	d->visited = 1;
	for (int i = 0; i < d->ndeps; i++)
		order_init(d->deps[i], out, n);
	out[(*n)++] = d;
}

static void run_init(struct dso *d)
{
	/* on the destructor list first: exit() from a constructor still
	 * runs this object's destructors */
	d->inited = 1;
	d->fini_next = fini_head;
	fini_head = d;
	for (size_t i = 0; i < d->preinit_n; i++)
		d->preinit_array[i]();
	if (d->init)
		((void (*)(void))d->init)();
	for (size_t i = 0; i < d->init_n; i++)
		d->init_array[i]();
}

static void run_fini(const struct dso *d)
{
	for (size_t i = d->fini_n; i-- > 0;)
		d->fini_array[i]();
	if (d->fini)
		((void (*)(void))d->fini)();
}

/* exit(): destructors in the reverse order of the constructors */
hidden void __dl_fini(void)
{
	dl_lock();
	struct dso *d;
	while ((d = fini_head)) {
		fini_head = d->fini_next;
		run_fini(d);
	}
	dl_unlock();
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
	(void)sp;
	uintptr_t base = (uintptr_t)&__ehdr_start;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + __ehdr_start.e_phoff);
	for (size_t i = 0; i < __ehdr_start.e_phnum; i++)
		if (ph[i].p_type == PT_DYNAMIC)
			__self_relocate(base, (const Elf64_Dyn *)(base + ph[i].p_vaddr), 0);
}

static void say_hex(uintptr_t v)
{
	char buf[19];
	buf[0] = '0';
	buf[1] = 'x';
	int n = 2;
	for (int s = 60; s >= 0; s -= 4)
		if (v >> s || s == 0 || n > 2)
			buf[n++] = "0123456789abcdef"[(v >> s) & 15];
	buf[n] = 0;
	say(buf);
}

/* ldd: what the program loads, and where */
static __attribute__((__noreturn__)) void list_objects(void)
{
	for (struct dso *d = head; d; d = d->next) {
		if (d == &app)
			continue;
		say("\t");
		say(d == &self ? "libc.so" : d->shortname);
		say(" => ");
		say(d->path);
		say(" (");
		say_hex((uintptr_t)d->map);
		say(")\n");
	}
	__syscall1(SYS_exit_group, 0);
	for (;;) ;
}

static const char *base_name(const char *s)
{
	const char *b = strrchr(s, '/');
	return b ? b + 1 : s;
}

hidden uintptr_t __dls_start(long **spp)
{
	long *sp = *spp;
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	char **envp = argv + argc + 1;
	size_t aux[AUX_CNT];
	size_t *auxv = __auxv_of(envp, aux);

	/* 1. ourselves: __dls_relocate_self has applied the relative
	 * relocations */
	uintptr_t base = (uintptr_t)&__ehdr_start;
	self.base = base;
	self.phdr = (const Elf64_Phdr *)(base + __ehdr_start.e_phoff);
	self.phnum = __ehdr_start.e_phnum;
	self.name = (char *)"libc.so";
	self.shortname = "libc.so";
	self.global = self.permanent = 1;
	set_extent(&self);
	secure = aux[AT_SECURE] != 0 || aux[AT_UID] != aux[AT_EUID] || aux[AT_GID] != aux[AT_EGID];

	/* 2. the program: mapped by the kernel, or, when libc.so itself is
	 * the program, by us */
	app.name = (char *)"";
	app.global = app.permanent = 1;
	uintptr_t entry = aux[AT_ENTRY];
	int list_only = 0;
	if (aux[AT_PHDR] == (size_t)self.phdr) {
		/* libc's functions (errno) need a thread pointer before the
		 * real one can be made: that needs the program's TLS size */
		static struct pthread early;
		__init_tp(&early);
		self.name = argv[0];
		int skip = 1;
		if (!strcmp(base_name(argv[0]), "ldd"))
			list_only = 1;
		else if (argc > 1 && !strcmp(argv[1], "--list"))
			list_only = 1, skip = 2;
		if (argc <= skip) {
			say("usage: libc.so [--list] program [arguments]\n");
			__syscall1(SYS_exit_group, 1);
		}
		const char *prog = argv[skip];
		app.path = prog;
		int fd = open(prog, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			fatal("cannot open ", prog, 0);
		entry = map_library(fd, &app, 1);
		close(fd);
		int dynamic = 0;
		for (size_t i = 0; i < app.phnum; i++)
			dynamic |= app.phdr[i].p_type == PT_DYNAMIC;
		if (!dynamic)
			fatal("not a dynamically linked program: ", prog, 0);
		/* the stack the program would have had: our own arguments
		 * dropped, the aux vector describing it */
		sp += skip;
		sp[0] = argc - skip;
		*spp = sp;
		argc -= skip;
		argv += skip;
		for (size_t *a = auxv; a[0]; a += 2) {
			switch (a[0]) {
			case AT_PHDR: a[1] = (size_t)app.phdr; break;
			case AT_PHNUM: a[1] = app.phnum; break;
			case AT_ENTRY: a[1] = entry; break;
			case AT_BASE: a[1] = base; break;
			case AT_EXECFN: a[1] = (size_t)prog; break;
			}
		}
		__auxv_of(envp, aux);
	} else {
		app.phdr = (const Elf64_Phdr *)aux[AT_PHDR];
		app.phnum = aux[AT_PHNUM];
		app.base = 0;
		for (size_t i = 0; i < app.phnum; i++)
			if (app.phdr[i].p_type == PT_PHDR)
				app.base = aux[AT_PHDR] - app.phdr[i].p_vaddr;
		/* its path, for $ORIGIN */
		app.path = aux[AT_EXECFN] ? (const char *)aux[AT_EXECFN] : argc > 0 && argv[0] ? argv[0] : "";
		for (size_t i = 0; i < app.phnum; i++)
			if (app.phdr[i].p_type == PT_INTERP)
				self.name = (char *)(app.base + app.phdr[i].p_vaddr);
		set_extent(&app);
	}
	self.path = self.name;

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
	for (int k = 0; k < 2; k++)
		if (mods[k]->tls_id)
			mods[k]->tls_offset = __libc.tls_mods[mods[k]->tls_id - 1].offset;
	static const unsigned char zero_rnd[16];
	__setup_tcb(aux[AT_RANDOM] ? (const unsigned char *)aux[AT_RANDOM] : zero_rnd, TLS_SURPLUS);
	__wipe_random(aux);
	size_t tls_capacity = __libc.tls_reserve;

	/* 4. bind libc against the program and itself, so the loader can use
	 * libc's functions from here on */
	decode(&app);
	decode(&self);
	head = &app;
	app.next = &self;
	relocate(&self, 1);

	/* 5. the libraries */
	for (char **e = envp; *e; e++) {
		if (!strncmp(*e, "LD_LIBRARY_PATH=", 16))
			env_library_path = *e + 16;
		else if (!strncmp(*e, "LD_PRELOAD=", 11))
			env_preload = *e + 11;
	}
	head = tail = 0;
	app.next = 0;
	adds = 0;
	append(&app);
	load_deps(&app);
	list_self();
	if (list_only)
		list_objects();
	assign_tls(head, tls_capacity);

	/* 6. bind everything else: dependencies before the objects that
	 * need them (the program last: its COPY relocations copy their
	 * data); tell debuggers where the object list is; make the
	 * relocated data read-only; copy the (now relocated) TLS images */
	int n = 0;
	for (struct dso *d = head; d; d = d->next)
		n++;
	struct dso **order = dl_alloc((size_t)n * sizeof *order);
	int norder = 0;
	order_init(&app, order, &norder);
	for (struct dso *d = head; d; d = d->next)
		order_init(d, order, &norder); /* preloads nothing needs */
	for (int k = 0; k < norder; k++)
		if (order[k] != &self)
			relocate(order[k], 0);
	_r_debug.r_version = 1;
	_r_debug.r_map = (struct link_map *)head;
	_r_debug.r_brk = (uintptr_t)_dl_debug_state;
	_r_debug.r_ldbase = base;
	for (Elf64_Dyn *v = (Elf64_Dyn *)app.dynv; v->d_tag; v++)
		if (v->d_tag == DT_DEBUG)
			v->d_un.d_ptr = (uintptr_t)&_r_debug;
	for (struct dso *d = head; d; d = d->next)
		protect_relro(d);
	__copy_tls((uintptr_t)__self());
	/* what dlopen may use in every thread */
	size_t want = ROUND_UP(__libc.tls_offset + DLOPEN_TLS, PAGE_SZ);
	__libc.tls_reserve = want < tls_capacity ? want : tls_capacity;

	/* 7. libc's state, then the constructors */
	__libc.dynamic = 1;
	__init_libc(argc, argv, envp, aux);
	started = 1;
	debug_event(RT_CONSISTENT);
	for (int k = 0; k < norder; k++)
		run_init(order[k]);
	return entry;
}

/* ---- dlopen and friends ---- */

static void rollback(struct dso *old_tail, int tls_count, size_t tls_offset)
{
	if (cur_fd >= 0) {
		close(cur_fd);
		cur_fd = -1;
	}
	struct dso *d = old_tail->next;
	while (d) {
		struct dso *next = d->next;
		if (d->map)
			munmap(d->map, d->map_len);
		subs++;
		free_dso(d);
		d = next;
	}
	old_tail->next = 0;
	tail = old_tail;
	__libc.tls_count = tls_count;
	__libc.tls_offset = tls_offset;
}

/* The part of dlopen that can fail (fail() returns to dlopen, which
 * undoes it). A function of its own, so that nothing dlopen keeps in
 * registers is changed between setjmp and longjmp. */
struct opening {
	struct dso *h, *first;   /* the handle; the first object loaded */
	struct dso **order;      /* the new objects, dependencies first */
	int norder, tls;
};

static __attribute__((__noinline__)) void open_objects(const char *file, int mode, struct dso *caller,
                                                       struct dso *old_tail, struct opening *o)
{
	struct dso *h;
	if (mode & RTLD_NOLOAD) {
		h = find_loaded(file);
		if (!h)
			fail("not loaded: ", file, 0);
	} else {
		h = load_library(file, caller, 1);
	}
	o->h = h;
	struct dso *first = old_tail->next;
	o->first = first;
	if (first) {
		load_deps(first);
		make_scope(h);
		for (struct dso *d = first; d; d = d->next) {
			d->lscope = h->scope;
			d->nlscope = h->nscope;
		}
		o->tls = assign_tls(first, __libc.tls_reserve);
		int n = 0;
		for (struct dso *d = first; d; d = d->next)
			n++;
		o->order = dl_alloc((size_t)n * sizeof *o->order);
		order_init(h, o->order, &o->norder);
		for (int k = 0; k < o->norder; k++)
			relocate(o->order[k], 0);
		for (struct dso *d = first; d; d = d->next) {
			protect_relro(d);
			d->lscope = 0; /* h may be unloaded before d */
			d->nlscope = 0;
		}
	}
	make_scope(h);
}

void *dlopen(const char *file, int mode)
{
	if (!file)
		return &app; /* the global scope */
	if (!(mode & (RTLD_LAZY | RTLD_NOW))) {
		set_error("invalid dlopen mode", 0);
		return 0;
	}
	uintptr_t ra = (uintptr_t)__builtin_return_address(0);
	dl_lock();
	struct dso *caller = addr_dso(ra);
	if (!caller)
		caller = &app;
	struct dso *old_tail = tail;
	int old_tls_count = __libc.tls_count;
	size_t old_tls_offset = __libc.tls_offset;
	jmp_buf jb, *prev = fail_jb;
	struct opening o = { 0 };
	debug_event(RT_ADD);
	if (setjmp(jb)) {
		fail_jb = prev;
		rollback(old_tail, old_tls_count, old_tls_offset);
		free(o.order);
		debug_event(RT_CONSISTENT);
		set_error(fail_msg, 0);
		dl_unlock();
		return 0;
	}
	fail_jb = &jb;
	open_objects(file, mode, caller, old_tail, &o);
	fail_jb = prev;

	/* nothing can fail from here on */
	struct dso *h = o.h;
	if (o.tls)
		__for_each_thread(init_tls, o.first);
	h->opens++;
	for (int i = 0; i < h->nscope; i++) {
		h->scope[i]->refcnt++;
		if (mode & RTLD_GLOBAL)
			h->scope[i]->global = 1;
	}
	if (mode & RTLD_NODELETE)
		h->nodelete = 1;
	debug_event(RT_CONSISTENT);
	for (int k = 0; k < o.norder; k++)
		run_init(o.order[k]);
	free(o.order);
	dl_unlock();
	return h;
}

static void keep_deps(struct dso *d)
{
	for (int i = 0; i < d->ndeps; i++) {
		if (d->deps[i]->doomed) {
			d->deps[i]->doomed = 0;
			keep_deps(d->deps[i]);
		}
	}
}

int dlclose(void *p)
{
	dl_lock();
	struct dso *h = valid_handle(p);
	if (h == &app) {
		dl_unlock();
		return 0;
	}
	if (!h || !h->opens) {
		set_error("invalid handle", 0);
		dl_unlock();
		return -1;
	}
	h->opens--;
	for (int i = 0; i < h->nscope; i++) {
		struct dso *d = h->scope[i];
		d->doomed = !--d->refcnt && !d->permanent && !d->nodelete && !d->tls_id;
	}
	/* what stays keeps its dependencies */
	for (struct dso *d = head; d; d = d->next)
		if (!d->doomed)
			keep_deps(d);
	int any = 0;
	for (struct dso *d = head; d; d = d->next)
		any |= d->doomed;
	if (any) {
		debug_event(RT_DELETE);
		/* destructors, in the usual order (a destructor may itself
		 * call dlclose: start over each time) */
		for (;;) {
			struct dso **pp = &fini_head;
			while (*pp && !(*pp)->doomed)
				pp = &(*pp)->fini_next;
			struct dso *d = *pp;
			if (!d)
				break;
			*pp = d->fini_next;
			run_fini(d);
		}
		/* atexit and C++ static destructors registered by them */
		for (struct dso *d = head; d; d = d->next)
			if (d->doomed)
				__exit_fns_in_range((uintptr_t)d->map, (uintptr_t)d->map + d->map_len);
		for (struct dso *d = head, *next; d; d = next) {
			next = d->next;
			if (!d->doomed)
				continue;
			unlink_dso(d);
			munmap(d->map, d->map_len);
			free_dso(d);
		}
		debug_event(RT_CONSISTENT);
	}
	dl_unlock();
	return 0;
}

static void *sym_addr(struct dso *d, const Elf64_Sym *s)
{
	if (ELF64_ST_TYPE(s->st_info) == STT_TLS)
		return (char *)__self() - d->tls_offset + s->st_value;
	return (void *)(d->base + s->st_value);
}

void *dlsym(void *restrict p, const char *restrict name)
{
	uintptr_t ra = (uintptr_t)__builtin_return_address(0);
	dl_lock();
	struct name nm = mkname(name);
	const Elf64_Sym *s = 0;
	struct dso *def = 0;
	if (p == RTLD_DEFAULT || p == &app) {
		struct dso *caller = addr_dso(ra);
		s = lookup(&nm, caller ? caller->scope : 0, caller ? caller->nscope : 0, 0, &def);
	} else if (p == RTLD_NEXT) {
		struct dso *caller = addr_dso(ra);
		if (!caller) {
			set_error("RTLD_NEXT used outside a loaded object", 0);
			dl_unlock();
			return 0;
		}
		for (struct dso *d = caller->next; d && !s; d = d->next)
			if (d->global && (s = lookup_in(d, &nm)))
				def = d;
	} else {
		struct dso *h = valid_handle(p);
		if (!h || !h->scope || (!h->permanent && !h->opens)) {
			set_error("invalid handle", 0);
			dl_unlock();
			return 0;
		}
		s = lookup_scope(h->scope, h->nscope, &nm, 0, &def);
	}
	if (s && ELF64_ST_TYPE(s->st_info) == STT_GNU_IFUNC)
		s = 0;
	void *r = s ? sym_addr(def, s) : 0;
	if (!s)
		set_error("symbol not found: ", name);
	dl_unlock();
	return r;
}

char *dlerror(void)
{
	if (!err_set)
		return 0;
	err_set = 0;
	return err_buf;
}

static size_t count_syms(const struct dso *d)
{
	if (d->hashtab)
		return d->hashtab[1];
	const uint32_t *ht = d->ghashtab;
	uint32_t nbuckets = ht[0], symoffset = ht[1], bloom_size = ht[2];
	const uint32_t *buckets = ht + 4 + 2 * bloom_size;
	const uint32_t *chain = buckets + nbuckets;
	uint32_t max = 0;
	for (uint32_t i = 0; i < nbuckets; i++)
		if (buckets[i] > max)
			max = buckets[i];
	if (max < symoffset)
		return symoffset;
	while (!(chain[max - symoffset] & 1))
		max++;
	return max + 1;
}

int dladdr(const void *addr, Dl_info *info)
{
	dl_lock();
	struct dso *d = addr_dso((uintptr_t)addr);
	if (!d) {
		dl_unlock();
		return 0;
	}
	info->dli_fname = d == &app ? app.path : d->name;
	info->dli_fbase = d->map;
	info->dli_sname = 0;
	info->dli_saddr = 0;
	const Elf64_Sym *best = 0;
	uintptr_t a = (uintptr_t)addr;
	size_t n = count_syms(d);
	for (size_t i = 1; i < n; i++) {
		const Elf64_Sym *s = &d->syms[i];
		int t = ELF64_ST_TYPE(s->st_info);
		if (!defined(s) || t == STT_TLS || t == STT_GNU_IFUNC)
			continue;
		uintptr_t v = d->base + s->st_value;
		if (v <= a && (!best || v > d->base + best->st_value))
			best = s;
	}
	if (best) {
		uintptr_t v = d->base + best->st_value;
		if (a == v || a - v < best->st_size) {
			info->dli_sname = d->strings + best->st_name;
			info->dli_saddr = (void *)v;
		}
	}
	dl_unlock();
	return 1;
}

int dlinfo(void *restrict p, int req, void *restrict arg)
{
	dl_lock();
	struct dso *h = valid_handle(p);
	int r = 0;
	if (!h) {
		set_error("invalid handle", 0);
		r = -1;
	} else if (req == RTLD_DI_LINKMAP) {
		*(struct link_map **)arg = (struct link_map *)h;
	} else if (req == RTLD_DI_ORIGIN) {
		const char *path = h == &app ? app.path : h->path;
		size_t l = dir_len(path);
		if (!l)
			memcpy(arg, ".", 2);
		else {
			memcpy(arg, path, l);
			((char *)arg)[l] = 0;
		}
	} else if (req == RTLD_DI_TLS_MODID) {
		*(size_t *)arg = (size_t)h->tls_id;
	} else if (req == RTLD_DI_TLS_DATA) {
		*(void **)arg = h->tls_id ? (char *)__self() - h->tls_offset : 0;
	} else {
		set_error("unsupported dlinfo request", 0);
		r = -1;
	}
	dl_unlock();
	return r;
}

/* __cxa_thread_atexit_impl: an object with a thread_local destructor
 * cannot be unloaded */
hidden void __dl_pin(const void *addr)
{
	dl_lock();
	struct dso *d = addr_dso((uintptr_t)addr);
	if (d)
		d->nodelete = 1;
	dl_unlock();
}

/* ---- dl_iterate_phdr and _dl_find_object ---- */

static void phdr_info(const struct dso *d, struct dl_phdr_info *info)
{
	*info = (struct dl_phdr_info){ 0 };
	info->dlpi_addr = d->base;
	info->dlpi_name = d->name;
	info->dlpi_phdr = d->phdr;
	info->dlpi_phnum = (Elf64_Half)d->phnum;
	if (d->tls_id) {
		info->dlpi_tls_modid = (size_t)d->tls_id;
		info->dlpi_tls_data = (char *)__self() - d->tls_offset;
	}
}

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, size_t, void *), void *arg)
{
	struct dl_phdr_info info;
	int r = 0;
	dl_lock();
	int vdso = __vdso_info(&info);
	unsigned long long a = adds + (unsigned)vdso;
	for (struct dso *d = head; d && !r; d = d->next) {
		phdr_info(d, &info);
		info.dlpi_adds = a;
		info.dlpi_subs = subs;
		r = cb(&info, sizeof info, arg);
	}
	if (!r && __vdso_info(&info)) {
		info.dlpi_adds = a;
		info.dlpi_subs = subs;
		r = cb(&info, sizeof info, arg);
	}
	dl_unlock();
	return r;
}

static int find_in(const struct dl_phdr_info *info, uintptr_t pc, struct dl_find_object *r, struct link_map *map)
{
	uintptr_t lo = UINTPTR_MAX, hi = 0;
	int inside = 0;
	void *eh = 0;
	for (size_t k = 0; k < info->dlpi_phnum; k++) {
		const Elf64_Phdr *p = &info->dlpi_phdr[k];
		if (p->p_type == PT_LOAD) {
			uintptr_t s = info->dlpi_addr + p->p_vaddr, e = s + p->p_memsz;
			inside |= pc >= s && pc < e;
			if (s < lo)
				lo = s;
			if (e > hi)
				hi = e;
		} else if (p->p_type == PT_GNU_EH_FRAME) {
			eh = (void *)(info->dlpi_addr + p->p_vaddr);
		}
	}
	if (!inside)
		return 0;
	*r = (struct dl_find_object){ 0 };
	r->dlfo_map_start = (void *)lo;
	r->dlfo_map_end = (void *)hi;
	r->dlfo_link_map = map;
	r->dlfo_eh_frame = eh;
	return 1;
}

int _dl_find_object(void *pc, struct dl_find_object *r)
{
	static struct link_map vdso_map;
	struct dl_phdr_info info;
	int found = 0;
	dl_lock();
	struct dso *d = addr_dso((uintptr_t)pc);
	if (d) {
		phdr_info(d, &info);
		found = find_in(&info, (uintptr_t)pc, r, (struct link_map *)d);
	} else if (__vdso_info(&info)) {
		vdso_map.l_addr = info.dlpi_addr;
		vdso_map.l_name = (char *)info.dlpi_name;
		found = find_in(&info, (uintptr_t)pc, r, &vdso_map);
	}
	dl_unlock();
	return found ? 0 : -1;
}

#endif
