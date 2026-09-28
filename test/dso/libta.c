/* Test library: needs libtb.so (found through $ORIGIN), has TLS the
 * program uses directly, and a function the program overrides. */
extern void log_event(const char *);
int tb_value(void);

__thread int ta_tls = 44;

int ta_value(void) { return tb_value() * 10 + 1; }
const char *hook(void) { return "ta"; }
const char *ta_calls_hook(void) { return hook(); }

__attribute__((constructor)) static void ctor(void) { log_event("ctor ta"); }
__attribute__((destructor)) static void dtor(void) { log_event("dtor ta"); }
