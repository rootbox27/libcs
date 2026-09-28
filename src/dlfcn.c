/* <dlfcn.h> for static executables (libc.so: ldso/dynlink.c). */
#ifndef CITADEL_SHARED
#include <dlfcn.h>
#include <string.h>

static _Thread_local const char *err;
static char self;

void *dlopen(const char *file, int mode)
{
	(void)mode;
	if (!file)
		return &self; /* the program itself */
	err = "Dynamic loading not supported";
	return 0;
}

int dlclose(void *h)
{
	if (h != &self) {
		err = "Invalid handle";
		return -1;
	}
	return 0;
}

void *dlsym(void *restrict h, const char *restrict name)
{
	(void)h;
	(void)name;
	err = "Symbol not found";
	return 0;
}

char *dlerror(void)
{
	const char *e = err;
	err = 0;
	return (char *)e;
}

int dladdr(const void *addr, Dl_info *info)
{
	(void)addr;
	memset(info, 0, sizeof *info);
	return 0;
}

int dlinfo(void *restrict h, int req, void *restrict arg)
{
	(void)req;
	(void)arg;
	err = h == &self ? "Unsupported request" : "Invalid handle";
	return -1;
}
#endif
