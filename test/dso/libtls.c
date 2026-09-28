/* dlopen test: thread-local storage in a library loaded after threads
 * exist, reached through __tls_get_addr, initial-exec and a TLS
 * descriptor (written in assembly: not every compiler has
 * -mtls-dialect=gnu2; built with -mno-red-zone, as the call in the asm
 * pushes below the stack pointer). */
__thread int tls_gd = 5;
__attribute__((tls_model("initial-exec"))) __thread int tls_ie = 6;
static int target;
__thread int *tls_ptr = &target; /* the TLS image holds a relocated pointer */

int *tls_gd_addr(void) { return &tls_gd; }
int *tls_ie_addr(void) { return &tls_ie; }
int tls_ptr_ok(void) { return tls_ptr == &target; }

int *tls_desc_addr(void)
{
	void *r;
	__asm__("lea tls_gd@TLSDESC(%%rip), %%rax\n\t"
	        "call *tls_gd@TLSCALL(%%rax)\n\t"
	        "add %%fs:0, %%rax"
	        : "=a"(r) : : "memory", "cc");
	return r;
}
