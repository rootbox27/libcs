/* dlopen test: a dependency of libtc.so. */
extern void log_event(const char *);

int td_value(void) { return 7; }

__attribute__((constructor)) static void ctor(void) { log_event("ctor td"); }
__attribute__((destructor)) static void dtor(void) { log_event("dtor td"); }
