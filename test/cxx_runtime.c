/* Interfaces the C++ runtime needs: dl_iterate_phdr and _dl_find_object
 * (tested through GCC's unwinder), __cxa_thread_atexit_impl,
 * pthread_cond_clockwait and <linux/futex.h>. */
#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>
#include "harness.h"

int __cxa_thread_atexit_impl(void (*)(void *), void *, void *);
extern void *__dso_handle;

static __thread int tls_var = 42;

/* ---- dl_iterate_phdr ---- */

struct seen {
	int objects, exe_ok, eh_ok, tls_ok, vdso;
};

static int visit(struct dl_phdr_info *info, size_t size, void *arg)
{
	struct seen *s = arg;
	s->objects++;
	if (size < sizeof *info)
		return 1;
	if (info->dlpi_name && !strcmp(info->dlpi_name, "linux-vdso.so.1"))
		s->vdso = 1;
	uintptr_t pc = (uintptr_t)&visit;
	for (int i = 0; i < info->dlpi_phnum; i++) {
		const ElfW(Phdr) *p = &info->dlpi_phdr[i];
		uintptr_t start = info->dlpi_addr + p->p_vaddr;
		if (p->p_type == PT_LOAD && pc >= start && pc < start + p->p_memsz)
			s->exe_ok = 1;
		if (p->p_type == PT_GNU_EH_FRAME)
			s->eh_ok = 1;
		if (p->p_type == PT_TLS && info->dlpi_tls_modid == 1) {
			uintptr_t t = (uintptr_t)&tls_var, b = (uintptr_t)info->dlpi_tls_data;
			s->tls_ok = t >= b && t < b + p->p_memsz;
		}
	}
	return 0;
}

static int stop_at_first(struct dl_phdr_info *info, size_t size, void *arg)
{
	++*(int *)arg;
	return 7;
}

static void phdrs(void)
{
	struct seen s = { 0 };
	CHECK(dl_iterate_phdr(visit, &s) == 0);
	CHECK(s.objects >= 1 && s.exe_ok && s.eh_ok && s.tls_ok);
	int calls = 0;
	CHECK(dl_iterate_phdr(stop_at_first, &calls) == 7 && calls == 1);

	struct dl_find_object fo;
	CHECK(_dl_find_object((void *)&phdrs, &fo) == 0);
	CHECK(fo.dlfo_eh_frame != 0 && (uintptr_t)fo.dlfo_map_start <= (uintptr_t)&phdrs &&
	      (uintptr_t)&phdrs < (uintptr_t)fo.dlfo_map_end);
	int local;
	CHECK(_dl_find_object(&local, &fo) == -1);
}

/* ---- the unwinder finds our frames ---- */

static int frames;
static uintptr_t ips[16];

static _Unwind_Reason_Code trace(struct _Unwind_Context *c, void *a)
{
	if (frames < 16)
		ips[frames] = _Unwind_GetIP(c);
	frames++;
	return _URC_NO_REASON;
}

__attribute__((noinline)) static int depth3(void)
{
	_Unwind_Backtrace(trace, 0);
	return frames;
}
__attribute__((noinline)) static int depth2(void) { return depth3() + 1; }
__attribute__((noinline)) static int depth1(void) { return depth2() + 1; }

static void unwind(void)
{
	CHECK(depth1() > 0);
	/* depth3, depth2, depth1, unwind, main at least */
	CHECK(frames >= 5);
	struct dl_find_object fo;
	CHECK(_dl_find_object((void *)ips[1], &fo) == 0);
}

/* ---- thread_local destructors ---- */

static char order[16];
static int norder;
static pthread_key_t key;

static void note(void *p)
{
	order[norder++] = *(char *)p;
}

static void late(void *p)
{
	order[norder++] = 'L';
}

static void registers_more(void *p)
{
	order[norder++] = *(char *)p;
	/* registered during destruction: must still run */
	__cxa_thread_atexit_impl(late, 0, __dso_handle);
}

static void tsd_dtor(void *p)
{
	order[norder++] = 'K';
}

static void *thread_fn(void *arg)
{
	static char a = 'a', b = 'b', c = 'c';
	pthread_setspecific(key, (void *)1);
	CHECK(__cxa_thread_atexit_impl(note, &a, __dso_handle) == 0);
	CHECK(__cxa_thread_atexit_impl(registers_more, &b, __dso_handle) == 0);
	CHECK(__cxa_thread_atexit_impl(note, &c, __dso_handle) == 0);
	return 0;
}

static int main_tls_ran;

static void main_tls(void *p)
{
	main_tls_ran = 1;
}

static void at_exit_check(void)
{
	/* the main thread's thread_local destructors run before atexit */
	if (!main_tls_ran)
		_exit(3);
}

static void thread_dtors(void)
{
	CHECK(pthread_key_create(&key, tsd_dtor) == 0);
	pthread_t t;
	CHECK(pthread_create(&t, 0, thread_fn, 0) == 0 && pthread_join(t, 0) == 0);
	/* reverse order, the one registered during destruction too, and all
	 * before the pthread key destructor */
	order[norder] = 0;
	CHECK(!strcmp(order, "cbLaK"));
	atexit(at_exit_check);
	CHECK(__cxa_thread_atexit_impl(main_tls, 0, __dso_handle) == 0);
}

/* ---- pthread_cond_clockwait ---- */

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready;

static void *signaller(void *arg)
{
	pthread_mutex_lock(&m);
	ready = 1;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&m);
	return 0;
}

static void clockwait(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_nsec += 20000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	pthread_mutex_lock(&m);
	CHECK(pthread_cond_clockwait(&cv, &m, CLOCK_MONOTONIC, &ts) == ETIMEDOUT);
	CHECK(pthread_cond_clockwait(&cv, &m, CLOCK_PROCESS_CPUTIME_ID, &ts) == EINVAL);
	CHECK(pthread_cond_clockwait(&cv, &m, CLOCK_MONOTONIC, 0) == EINVAL);
	pthread_t t;
	CHECK(pthread_create(&t, 0, signaller, 0) == 0);
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 10;
	int r = 0;
	while (!ready && r == 0)
		r = pthread_cond_clockwait(&cv, &m, CLOCK_REALTIME, &ts);
	CHECK(ready && r == 0);
	pthread_mutex_unlock(&m);
	pthread_join(t, 0);
}

static void futex_header(void)
{
	CHECK(FUTEX_WAKE_PRIVATE == 129 && FUTEX_WAIT_BITSET_PRIVATE == 137 && FUTEX_BITSET_MATCH_ANY == 0xffffffffu);
	int word = 0;
	CHECK(syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, 0, 0, 0) == 0);
	struct timespec zero = { 0, 0 };
	errno = 0;
	CHECK(syscall(SYS_futex, &word, FUTEX_WAIT_BITSET_PRIVATE, 0, &zero, 0, FUTEX_BITSET_MATCH_ANY) == -1 &&
	      errno == ETIMEDOUT);
}

int main(void)
{
	phdrs();
	unwind();
	thread_dtors();
	clockwait();
	futex_header();
	return t_done();
}
