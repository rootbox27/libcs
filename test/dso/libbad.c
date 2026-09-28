/* dlopen test: needs libtf.so and a symbol nothing defines, so dlopen
 * fails and must unload libtf.so again without running anything. */
extern void log_event(const char *);
void no_such_symbol(void);
void bad(void) { no_such_symbol(); }
__attribute__((constructor)) static void ctor(void) { log_event("ctor bad"); }
