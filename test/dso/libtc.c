/* dlopen test: needs libtd.so; registers an atexit handler, which
 * dlclose runs before it unmaps the library. */
#include <stdlib.h>
extern void log_event(const char *);
int td_value(void);

int tc_data = 42;
int tc_fn(void) { return td_value() * 100 + tc_data; }

static void at_exit(void) { log_event("atexit tc"); }

__attribute__((constructor)) static void ctor(void)
{
	log_event("ctor tc");
	atexit(at_exit);
}
__attribute__((destructor)) static void dtor(void) { log_event("dtor tc"); }
