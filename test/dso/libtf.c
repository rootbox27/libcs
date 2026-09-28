/* dlopen test: loaded for libbad.so, then unloaded when that fails. */
extern void log_event(const char *);
__attribute__((constructor)) static void ctor(void) { log_event("ctor tf"); }
