/* Process startup: argv/envp/auxv, TLS initialisation, the stack canary,
 * constructors, and exit-time handler ordering. */
#include "harness.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <unistd.h>

static __thread int tls_int = 1234;
static __thread char tls_str[5] = "tls!";
static __thread long tls_zero[4];
static __thread char tls_big[100] __attribute__((__aligned__(32))) = "aligned";

static int ctor_ran;
__attribute__((__constructor__)) static void ctor(void) { ctor_ran = 1; }

static int exit_step;
/* atexit handlers must run in reverse order of registration; the last to
 * run reports the verdict through _Exit, overriding main's status. */
static void on_exit1(void) { _Exit(exit_step == 2 ? 0 : 20); }
static void on_exit2(void) { exit_step = exit_step == 1 ? 2 : -100; }
static void on_exit3(void) { exit_step = exit_step == 0 ? 1 : -100; }

extern char **environ;

int main(int argc, char **argv, char **envp)
{
	CHECK(argc == 3);
	CHECK(argc == 3 && strcmp(argv[1], "alpha") == 0 && strcmp(argv[2], "beta") == 0);
	CHECK(argv[argc] == 0);
	CHECK(environ == envp);
	int found = 0;
	for (char **e = envp; *e; e++)
		if (!strcmp(*e, "CITADEL_TEST=1"))
			found = 1;
	CHECK(found);
	CHECK(strrchr(argv[0], '/') ? !strcmp(program_invocation_short_name, strrchr(argv[0], '/') + 1)
	                            : !strcmp(program_invocation_short_name, argv[0]));
	CHECK(program_invocation_name == argv[0]);

	CHECK(ctor_ran);

	/* TLS: initialised data, zeroed bss and requested alignment */
	CHECK(tls_int == 1234);
	CHECK(!strcmp(tls_str, "tls!"));
	CHECK(tls_zero[0] == 0 && tls_zero[3] == 0);
	CHECK(!strcmp(tls_big, "aligned"));
	CHECK((uintptr_t)tls_big % 32 == 0);
	tls_int = 5;
	CHECK(tls_int == 5);
	errno = 0;
	CHECK(errno == 0);
	errno = EINVAL;
	CHECK(errno == EINVAL);
	CHECK(tls_int == 5 && !strcmp(tls_str, "tls!"));

	/* thread pointer and stack canary */
	uintptr_t self, canary, guard;
	__asm__("mov %%fs:0, %0" : "=r"(self));
	__asm__("mov %%fs:0x28, %0" : "=r"(canary));
	__asm__("mov %%fs:0x30, %0" : "=r"(guard));
	CHECK(self != 0);
	CHECK((canary & 0xff) == 0);
	CHECK(canary != 0);
	CHECK(guard != 0);
	/* TLS lives just below the thread pointer (variant II) */
	CHECK((uintptr_t)&tls_int < self && self - (uintptr_t)&tls_int < 4096);

	/* auxv: AT_RANDOM bytes are wiped after being consumed */
	CHECK(getauxval(AT_PAGESZ) == 4096);
	const unsigned char *rnd = (const unsigned char *)getauxval(AT_RANDOM);
	CHECK(rnd != 0);
	if (rnd) {
		unsigned char z = 0;
		for (int i = 0; i < 16; i++)
			z |= rnd[i];
		CHECK(z == 0);
	}
	CHECK(getauxval(0x7fff) == 0);
	CHECK(issetugid() == 0);

	CHECK(getpid() > 0);
	CHECK(gettid() == getpid());

	if (t_failures)
		return t_done();
	CHECK(atexit(on_exit1) == 0);
	CHECK(atexit(on_exit2) == 0);
	CHECK(atexit(on_exit3) == 0);
	return 30; /* replaced by on_exit1's _Exit status */
}
