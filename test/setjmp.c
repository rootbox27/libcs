#include "harness.h"
#include <setjmp.h>
#include <stdint.h>
#include <sys/syscall.h>

static jmp_buf jb;
static volatile int depth;

static void jump_from_deep(int n);
/* Called through a volatile pointer so the compiler can neither inline
 * the recursion nor misdiagnose it as infinite. */
static void (*volatile recurse)(int) = jump_from_deep;

static void jump_from_deep(int n)
{
	if (n > 0) {
		depth++;
		recurse(n - 1);
		depth += 1000; /* unreachable: the jump skips every frame */
		return;
	}
	longjmp(jb, 7);
}

/* Read the current signal mask with the raw system call. */
static unsigned long cur_mask(void)
{
	unsigned long old = 0;
	register long r10 __asm__("r10") = 8;
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"((long)SYS_rt_sigprocmask), "D"(0L), "S"(0L), "d"(&old), "r"(r10) : "rcx", "r11", "memory");
	return old;
}
static void set_mask(unsigned long m)
{
	register long r10 __asm__("r10") = 8;
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"((long)SYS_rt_sigprocmask), "D"(2L), "S"(&m), "d"(0L), "r"(r10) : "rcx", "r11", "memory");
}

int main(void)
{
	volatile int count = 0;
	int r = setjmp(jb);
	if (r == 0) {
		count++;
		recurse(10);
	}
	CHECK(r == 7);
	CHECK(count == 1);
	CHECK(depth == 10);

	/* longjmp(buf, 0) must make setjmp return 1 */
	r = setjmp(jb);
	if (r == 0)
		longjmp(jb, 0);
	CHECK(r == 1);

	/* rbp, rsp and rip are stored mangled, not as raw pointers */
	uintptr_t sp;
	__asm__("mov %%rsp, %0" : "=r"(sp));
	if (setjmp(jb) == 0) {
		uintptr_t saved_sp = jb[0].__jb[6];
		CHECK(saved_sp - sp > 0x100000 && sp - saved_sp > 0x100000);
	}

	/* sigsetjmp(.., 1) restores the mask; sigsetjmp(.., 0) does not */
	sigjmp_buf sj;
	unsigned long orig = cur_mask();
	unsigned long usr1 = 1UL << (10 - 1);
	if (sigsetjmp(sj, 1) == 0) {
		set_mask(orig | usr1);
		CHECK(cur_mask() & usr1);
		siglongjmp(sj, 1);
	}
	CHECK(cur_mask() == orig);
	if (sigsetjmp(sj, 0) == 0) {
		set_mask(orig | usr1);
		siglongjmp(sj, 1);
	}
	CHECK(cur_mask() & usr1);
	set_mask(orig);
	return t_done();
}
