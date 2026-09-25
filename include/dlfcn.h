#ifndef _DLFCN_H
#define _DLFCN_H
#include <features.h>
__BEGIN_DECLS
/* Only static executables are supported: dlopen always fails, but
 * dlopen(NULL) and dlsym on it behave as for a program with no dynamic
 * symbols. */
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_NODELETE 4096
#define RTLD_GLOBAL 256
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1)
typedef struct {
	const char *dli_fname;
	void *dli_fbase;
	const char *dli_sname;
	void *dli_saddr;
} Dl_info;
void *dlopen(const char *, int);
int dlclose(void *);
void *dlsym(void *__restrict, const char *__restrict);
char *dlerror(void);
int dladdr(const void *, Dl_info *);
__END_DECLS
#endif
