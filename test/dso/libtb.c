/* Test library: TLS in every model, data the program copies, a call back
 * into the program, a missing weak symbol. Built twice: TB_VALUE 2 in
 * lib/, 3 in alt/ (for LD_LIBRARY_PATH). */
extern void log_event(const char *);

int tb_counter = 100;
__thread int tb_gd = 11;
__attribute__((tls_model("initial-exec"))) __thread int tb_ie = 22;
static __thread int tb_local = 33;
__attribute__((weak)) void tb_missing(void);

int tb_value(void) { return TB_VALUE; }
int tb_bump(void) { return ++tb_counter; }
int *tb_gd_addr(void) { return &tb_gd; }
int *tb_ie_addr(void) { return &tb_ie; }
int *tb_local_addr(void) { return &tb_local; }
int tb_has_missing(void) { return tb_missing != 0; }

__attribute__((constructor)) static void ctor(void) { log_event("ctor tb"); }
__attribute__((destructor)) static void dtor(void) { log_event("dtor tb"); }
