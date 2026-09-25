/* Clocks, sleeping and interval timers.
 *
 * clock_gettime uses the kernel's vDSO when present (found through
 * AT_SYSINFO_EHDR) and falls back to the system call. The resolved
 * function pointer is kept mangled like other library function pointers. */
#include "internal.h"
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

typedef int (*cgt_fn)(clockid_t, struct timespec *);

/* Look a symbol up in the vDSO image. */
static void *vdso_sym(const char *name)
{
	uintptr_t base = getauxval(AT_SYSINFO_EHDR);
	if (!base)
		return 0;
	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)base;
	const Elf64_Phdr *ph = (const Elf64_Phdr *)(base + eh->e_phoff);
	uintptr_t bias = 0;
	const Elf64_Dyn *dyn = 0;
	int have_load = 0;
	for (int i = 0; i < eh->e_phnum; i++) {
		if (ph[i].p_type == PT_LOAD && !have_load) {
			bias = base + ph[i].p_offset - ph[i].p_vaddr;
			have_load = 1;
		} else if (ph[i].p_type == PT_DYNAMIC) {
			dyn = (const Elf64_Dyn *)(base + ph[i].p_offset);
		}
	}
	if (!have_load || !dyn)
		return 0;
	const Elf64_Sym *sym = 0;
	const char *str = 0;
	const uint32_t *hash = 0, *gnu = 0;
	for (; dyn->d_tag != DT_NULL; dyn++) {
		switch (dyn->d_tag) {
		case DT_SYMTAB: sym = (const Elf64_Sym *)(bias + dyn->d_un.d_ptr); break;
		case DT_STRTAB: str = (const char *)(bias + dyn->d_un.d_ptr); break;
		case DT_HASH: hash = (const uint32_t *)(bias + dyn->d_un.d_ptr); break;
		case DT_GNU_HASH: gnu = (const uint32_t *)(bias + dyn->d_un.d_ptr); break;
		}
	}
	if (!sym || !str || (!hash && !gnu))
		return 0;
	size_t nsym;
	if (hash) {
		nsym = hash[1];
	} else {
		/* GNU hash: highest bucket start, then walk its chain */
		uint32_t nbuckets = gnu[0], symoff = gnu[1], bloom = gnu[2];
		const uint32_t *buckets = gnu + 4 + bloom * 2;
		const uint32_t *chain = buckets + nbuckets;
		uint32_t max = 0;
		for (uint32_t i = 0; i < nbuckets; i++)
			if (buckets[i] > max)
				max = buckets[i];
		if (max) {
			while (!(chain[max - symoff] & 1))
				max++;
			max++;
		}
		nsym = max;
	}
	for (size_t i = 0; i < nsym; i++) {
		if (ELF64_ST_TYPE(sym[i].st_info) != STT_FUNC || sym[i].st_shndx == SHN_UNDEF)
			continue;
		int b = ELF64_ST_BIND(sym[i].st_info);
		if (b != STB_GLOBAL && b != STB_WEAK)
			continue;
		if (!strcmp(str + sym[i].st_name, name))
			return (void *)(bias + sym[i].st_value);
	}
	return 0;
}

static uintptr_t cgt_mangled;
static int cgt_state; /* 0 unresolved, 1 vdso, 2 syscall only */

int clock_gettime(clockid_t clk, struct timespec *ts)
{
	if (!cgt_state) {
		void *f = vdso_sym("__vdso_clock_gettime");
		if (f)
			cgt_mangled = __ptr_mangle((uintptr_t)f);
		__atomic_store_n(&cgt_state, f ? 1 : 2, __ATOMIC_RELEASE);
	}
	if (__atomic_load_n(&cgt_state, __ATOMIC_ACQUIRE) == 1) {
		int r = ((cgt_fn)__ptr_demangle(cgt_mangled))(clk, ts);
		if (!r)
			return 0;
		if (r != -ENOSYS && r != -EINVAL) {
			errno = -r;
			return -1;
		}
	}
	return (int)sys(SYS_clock_gettime, clk, ts);
}

int clock_getres(clockid_t clk, struct timespec *ts) { return (int)sys(SYS_clock_getres, clk, ts); }
int clock_settime(clockid_t clk, const struct timespec *ts) { return (int)sys(SYS_clock_settime, clk, ts); }

int timespec_get(struct timespec *ts, int base)
{
	if (base != TIME_UTC)
		return 0;
	return clock_gettime(CLOCK_REALTIME, ts) ? 0 : base;
}

time_t time(time_t *t)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	if (t)
		*t = ts.tv_sec;
	return ts.tv_sec;
}

int gettimeofday(struct timeval *__restrict tv, void *__restrict tz)
{
	if (tv) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	if (!tv)
		return 0;
	if ((unsigned long)tv->tv_usec >= 1000000) {
		errno = EINVAL;
		return -1;
	}
	struct timespec ts = { tv->tv_sec, tv->tv_usec * 1000 };
	return clock_settime(CLOCK_REALTIME, &ts);
}

double difftime(time_t a, time_t b)
{
	return (double)a - (double)b;
}

clock_t clock(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts))
		return (clock_t)-1;
	if (ts.tv_sec > LONG_MAX / 1000000 - 1)
		return (clock_t)-1;
	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

clock_t times(struct tms *t)
{
	return (clock_t)sys(SYS_times, t);
}

/* ---- sleeping ---- */

int clock_nanosleep(clockid_t clk, int flags, const struct timespec *req, struct timespec *rem)
{
	if (clk == CLOCK_THREAD_CPUTIME_ID)
		return EINVAL;
	__testcancel();
	int r = (int)-__sys(SYS_clock_nanosleep, clk, flags, req, rem);
	if (r == EINTR)
		__testcancel();
	return r;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	int r = clock_nanosleep(CLOCK_REALTIME, 0, req, rem);
	if (r) {
		errno = r;
		return -1;
	}
	return 0;
}

unsigned sleep(unsigned s)
{
	struct timespec ts = { s, 0 };
	if (nanosleep(&ts, &ts))
		return (unsigned)ts.tv_sec + (ts.tv_nsec > 0);
	return 0;
}

int usleep(useconds_t us)
{
	struct timespec ts = { us / 1000000, (long)(us % 1000000) * 1000 };
	return nanosleep(&ts, &ts);
}

int getitimer(int which, struct itimerval *v) { return (int)sys(SYS_getitimer, which, v); }
int setitimer(int which, const struct itimerval *__restrict v, struct itimerval *__restrict old)
{
	return (int)sys(SYS_setitimer, which, v, old);
}

unsigned alarm(unsigned s)
{
	struct itimerval v = { { 0, 0 }, { s, 0 } }, old = { { 0, 0 }, { 0, 0 } };
	setitimer(ITIMER_REAL, &v, &old);
	return (unsigned)old.it_value.tv_sec + !!old.it_value.tv_usec;
}

/* ---- file times ---- */

int utimes(const char *path, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (tv) {
		for (int i = 0; i < 2; i++) {
			if ((unsigned long)tv[i].tv_usec >= 1000000) {
				errno = EINVAL;
				return -1;
			}
			ts[i].tv_sec = tv[i].tv_sec;
			ts[i].tv_nsec = tv[i].tv_usec * 1000;
		}
	}
	return (int)sys(SYS_utimensat, AT_FDCWD, path, tv ? ts : 0, 0);
}

int utime(const char *path, const struct utimbuf *t)
{
	struct timespec ts[2];
	if (t) {
		ts[0] = (struct timespec){ t->actime, 0 };
		ts[1] = (struct timespec){ t->modtime, 0 };
	}
	return (int)sys(SYS_utimensat, AT_FDCWD, path, t ? ts : 0, 0);
}
