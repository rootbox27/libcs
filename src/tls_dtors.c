/* thread_local destructors: __cxa_thread_atexit_impl.
 *
 * The C++ runtime registers the destructor of each thread_local object
 * as it is constructed. They run in reverse order when the thread exits,
 * before its pthread-specific data destructors and while its TLS is still
 * there; for the main thread, in exit() before the atexit handlers. A
 * destructor may register more; those run too.
 *
 * exit() and pthread_exit() reach this file through a weak reference, so
 * programs that never register one (all of C) do not link it or malloc. */
#include "internal.h"
#include <stdlib.h>

struct tls_dtor {
	uintptr_t fn;
	void *obj;
	struct tls_dtor *next;
};

int __cxa_thread_atexit_impl(void (*fn)(void *), void *obj, void *dso)
{
	(void)dso; /* static binaries: no object can be unloaded */
	struct tls_dtor *d = malloc(sizeof *d);
	if (!d)
		return -1;
	struct pthread *self = __self();
	d->fn = __ptr_mangle((uintptr_t)fn);
	d->obj = obj;
	d->next = self->tls_dtors;
	self->tls_dtors = d;
	return 0;
}

hidden void __run_tls_dtors(void)
{
	struct pthread *self = __self();
	struct tls_dtor *d;
	while ((d = self->tls_dtors)) {
		self->tls_dtors = d->next;
		void (*fn)(void *) = (void (*)(void *))__ptr_demangle(d->fn);
		void *obj = d->obj;
		free(d);
		fn(obj);
	}
}
