/* dlopen, dlsym, dlclose, dladdr, dlinfo and the debugger interface
 * (dynamic linking stage 3 and 4). Its stdout, checked against
 * test/dso_dlopen.expected, shows the constructor, destructor and atexit
 * order. The libraries are in lib/, found through the program's
 * DT_RUNPATH. */
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "harness.h"

extern const ElfW(Dyn) _DYNAMIC[];

/* called by the libraries (the program exports its symbols) */
void log_event(const char *s)
{
	write(1, s, strlen(s));
	write(1, "\n", 1);
}

static int ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && !strcmp(s + a - b, tail);
}

static int has_error(const char *part)
{
	const char *e = dlerror();
	return e && strstr(e, part) && !dlerror();
}

struct count {
	int n;
	const char *name;
	int found;
	unsigned long long subs;
};

static int visit(struct dl_phdr_info *i, size_t size, void *arg)
{
	struct count *c = arg;
	c->n++;
	if (c->name && ends_with(i->dlpi_name, c->name))
		c->found = 1;
	c->subs = i->dlpi_subs;
	return 0;
}

static int loaded(const char *name)
{
	struct count c = { 0, name, 0, 0 };
	dl_iterate_phdr(visit, &c);
	return c.found;
}

static int objects(void)
{
	struct count c = { 0 };
	dl_iterate_phdr(visit, &c);
	return c.n;
}

/* the object list as a debugger sees it */
static int in_r_debug(const char *name)
{
	struct r_debug *r = 0;
	for (const ElfW(Dyn) *d = _DYNAMIC; d->d_tag; d++)
		if (d->d_tag == DT_DEBUG)
			r = (struct r_debug *)d->d_un.d_ptr;
	if (r != &_r_debug || r->r_version != 1 || r->r_state != RT_CONSISTENT || !r->r_brk)
		return -1;
	int found = 0;
	struct link_map *prev = 0;
	for (struct link_map *m = r->r_map; m; prev = m, m = m->l_next) {
		if (m->l_prev != prev)
			return -1;
		if (m->l_name && ends_with(m->l_name, name))
			found = 1;
	}
	return found;
}

static int *(*gd_addr)(void), *(*ie_addr)(void), *(*desc_addr)(void);
static int (*ptr_ok)(void);
static int pipefd[2];

static int tls_ok(void)
{
	return *gd_addr() == 5 && desc_addr() == gd_addr() && *ie_addr() == 6 && ptr_ok();
}

/* started before libtls.so is loaded, checks it afterwards */
static void *early_thread(void *arg)
{
	char c;
	*(int *)arg = read(pipefd[0], &c, 1) == 1 && tls_ok();
	return 0;
}

static void *late_thread(void *arg)
{
	*(int *)arg = tls_ok();
	*gd_addr() = 99;
	return 0;
}

int main(int argc, char **argv)
{
	log_event("main");
	CHECK(dlerror() == 0);

	/* the global scope */
	void *self = dlopen(0, RTLD_NOW);
	CHECK(self && dlsym(self, "strlen") == (void *)strlen);
	CHECK(dlsym(RTLD_DEFAULT, "log_event") == (void *)log_event);
	CHECK(dlsym(RTLD_NEXT, "strlen") == (void *)strlen);
	CHECK(!dlsym(self, "no_such_symbol") && has_error("no_such_symbol"));
	CHECK(!dlopen("libc.so", 0) && has_error("invalid"));

	/* failures leave nothing behind */
	int n0 = objects();
	CHECK(!dlopen("libnothere.so", RTLD_NOW) && has_error("cannot find library libnothere.so"));
	CHECK(!dlopen("libbad.so", RTLD_NOW) && has_error("symbol not found: no_such_symbol"));
	CHECK(objects() == n0 && !loaded("/libtf.so") && !loaded("/libbad.so"));
	CHECK(!dlopen("libtf.so", RTLD_NOW | RTLD_NOLOAD) && has_error("not loaded"));
	CHECK(!dlopen(argv[0], RTLD_NOW) && has_error("cannot load a program"));
	CHECK(objects() == n0);

	/* a library and its dependency, local */
	void *h = dlopen("libtc.so", RTLD_NOW);
	CHECK(h != 0);
	int (*tc_fn)(void) = (int (*)(void))dlsym(h, "tc_fn");
	int (*td_value)(void) = (int (*)(void))dlsym(h, "td_value");
	CHECK(tc_fn && tc_fn() == 742 && td_value && td_value() == 7);
	CHECK(*(int *)dlsym(h, "tc_data") == 42);
	CHECK(!dlsym(RTLD_DEFAULT, "tc_fn"));
	CHECK(dlopen("libtc.so", RTLD_NOW | RTLD_NOLOAD) == h);
	CHECK(dlclose(h) == 0);
	CHECK(loaded("/libtc.so") && loaded("/libtd.so") && in_r_debug("/libtc.so") == 1);

	Dl_info info;
	CHECK(dladdr((void *)tc_fn, &info) && ends_with(info.dli_fname, "/libtc.so") &&
	      !strcmp(info.dli_sname, "tc_fn") && info.dli_saddr == (void *)tc_fn && info.dli_fbase);
	CHECK(dladdr((char *)tc_fn + 1, &info) && info.dli_sname && !strcmp(info.dli_sname, "tc_fn"));
	CHECK(dladdr((void *)dlopen, &info) && strstr(info.dli_fname, "libc.so") && !strcmp(info.dli_sname, "dlopen"));
	CHECK(!dladdr((void *)16, &info));
	struct link_map *lm = 0;
	CHECK(dlinfo(h, RTLD_DI_LINKMAP, &lm) == 0 && lm && ends_with(lm->l_name, "/libtc.so"));
	size_t modid = 1;
	CHECK(dlinfo(h, RTLD_DI_TLS_MODID, &modid) == 0 && modid == 0);

	/* libte.so uses td_value without depending on libtd.so: it loads
	 * only once libtd.so is global, and then keeps it loaded */
	CHECK(!dlopen("libte.so", RTLD_NOW) && has_error("symbol not found: td_value"));
	CHECK(dlopen("libtc.so", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD) == h);
	CHECK(dlsym(RTLD_DEFAULT, "tc_fn") == (void *)tc_fn);
	void *e = dlopen("libte.so", RTLD_LAZY);
	CHECK(e != 0);
	int (*te_get)(void) = (int (*)(void))dlsym(e, "te_get");
	CHECK(te_get && te_get() == 7);
	CHECK(dlclose(e) == 0 && !loaded("/libte.so") && in_r_debug("/libte.so") == 0);

	/* unloading: destructors, then the library's atexit handlers; the
	 * pinned libtd.so stays */
	CHECK(dlclose(h) == 0 && loaded("/libtc.so")); /* opened twice */
	struct count before = { 0 };
	dl_iterate_phdr(visit, &before);
	CHECK(dlclose(h) == 0);
	CHECK(!loaded("/libtc.so") && loaded("/libtd.so") && in_r_debug("/libtc.so") == 0);
	struct count after = { 0 };
	dl_iterate_phdr(visit, &after);
	CHECK(after.subs == before.subs + 1);
	CHECK(!dladdr((void *)tc_fn, &info) || !strstr(info.dli_fname, "libtc"));
	CHECK(dlclose(h) == -1 && has_error("invalid handle"));
	CHECK(td_value() == 7);

	/* TLS in a library loaded after a thread was started */
	pthread_t t;
	int early = 0, late = 0;
	CHECK(pipe(pipefd) == 0);
	CHECK(pthread_create(&t, 0, early_thread, &early) == 0);
	void *tl = dlopen("libtls.so", RTLD_NOW);
	CHECK(tl != 0);
	gd_addr = (int *(*)(void))dlsym(tl, "tls_gd_addr");
	ie_addr = (int *(*)(void))dlsym(tl, "tls_ie_addr");
	desc_addr = (int *(*)(void))dlsym(tl, "tls_desc_addr");
	ptr_ok = (int (*)(void))dlsym(tl, "tls_ptr_ok");
	CHECK(gd_addr && ie_addr && desc_addr && ptr_ok);
	CHECK(tls_ok());
	CHECK(dlsym(tl, "tls_gd") == (void *)gd_addr());
	*gd_addr() = 50;
	CHECK(write(pipefd[1], "x", 1) == 1 && pthread_join(t, 0) == 0 && early);
	CHECK(pthread_create(&t, 0, late_thread, &late) == 0 && pthread_join(t, 0) == 0 && late);
	CHECK(*gd_addr() == 50);
	void *data = 0;
	CHECK(dlinfo(tl, RTLD_DI_TLS_MODID, &modid) == 0 && modid > 0);
	CHECK(dlinfo(tl, RTLD_DI_TLS_DATA, &data) == 0 && data);
	/* a library with TLS is never unloaded */
	CHECK(dlclose(tl) == 0 && loaded("/libtls.so") && *gd_addr() == 50);

	log_event("end");
	return t_done();
}
