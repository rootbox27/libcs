/* Floating-point environment (C11 7.6) for x86_64.
 *
 * Exception flags live in both the x87 status word and MXCSR; tests read
 * the union of the two, clears and sets touch both. The rounding mode is
 * written to both control registers. */
#include <fenv.h>

static unsigned get_mxcsr(void)
{
	unsigned m;
	__asm__ __volatile__("stmxcsr %0" : "=m"(m));
	return m;
}

static void set_mxcsr(unsigned m)
{
	__asm__ __volatile__("ldmxcsr %0" : : "m"(m));
}

static unsigned short x87_status(void)
{
	unsigned short sw;
	__asm__ __volatile__("fnstsw %0" : "=a"(sw));
	return sw;
}

int feclearexcept(int excepts)
{
	excepts &= FE_ALL_EXCEPT;
	if (!excepts)
		return 0;
	if (x87_status() & excepts) {
		fenv_t env;
		__asm__ __volatile__("fnstenv %0" : "=m"(env));
		env.__status_word &= ~excepts;
		__asm__ __volatile__("fldenv %0" : : "m"(env));
	}
	set_mxcsr(get_mxcsr() & ~(unsigned)excepts);
	return 0;
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = (fexcept_t)((x87_status() | get_mxcsr()) & excepts & FE_ALL_EXCEPT);
	return 0;
}

int feraiseexcept(int excepts)
{
	/* Raise through real operations so enabled traps fire as they would. */
	excepts &= FE_ALL_EXCEPT;
	if (excepts & FE_INVALID) {
		volatile float z = 0;
		z = z / z;
		(void)z;
	}
	if (excepts & FE_DIVBYZERO) {
		volatile float one = 1, z = 0;
		one = one / z;
		(void)one;
	}
	if (excepts & (FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)) {
		/* setting the SSE flag bits is enough for these */
		set_mxcsr(get_mxcsr() | (excepts & (FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)));
	}
	return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	excepts &= FE_ALL_EXCEPT;
	feclearexcept(excepts);
	set_mxcsr(get_mxcsr() | (*flagp & excepts));
	return 0;
}

int fetestexcept(int excepts)
{
	return (x87_status() | get_mxcsr()) & excepts & FE_ALL_EXCEPT;
}

int fegetround(void)
{
	return (get_mxcsr() >> 3) & 0xc00;
}

int fesetround(int round)
{
	if (round & ~0xc00)
		return -1;
	unsigned short cw;
	__asm__ __volatile__("fnstcw %0" : "=m"(cw));
	cw = (unsigned short)((cw & ~0xc00) | round);
	__asm__ __volatile__("fldcw %0" : : "m"(cw));
	set_mxcsr((get_mxcsr() & ~0x6000u) | ((unsigned)round << 3));
	return 0;
}

int fegetenv(fenv_t *envp)
{
	__asm__ __volatile__("fnstenv %0" : "=m"(*envp));
	/* fnstenv masks all x87 exceptions as a side effect; restore */
	__asm__ __volatile__("fldenv %0" : : "m"(*envp));
	envp->__mxcsr = get_mxcsr();
	return 0;
}

int feholdexcept(fenv_t *envp)
{
	fegetenv(envp);
	fenv_t env = *envp;
	env.__status_word &= ~FE_ALL_EXCEPT;
	env.__control_word |= FE_ALL_EXCEPT; /* mask all */
	__asm__ __volatile__("fldenv %0" : : "m"(env));
	set_mxcsr((envp->__mxcsr & ~(unsigned)FE_ALL_EXCEPT) | (FE_ALL_EXCEPT << 7));
	return 0;
}

int fesetenv(const fenv_t *envp)
{
	fenv_t env;
	__asm__ __volatile__("fnstenv %0" : "=m"(env));
	if (envp == FE_DFL_ENV) {
		env.__control_word = 0x037f;  /* all masked, round to nearest, 64-bit */
		env.__status_word &= ~0x38ff; /* flags and top-of-stack cleared */
		env.__tags = 0xffff;
		env.__eip = 0;
		env.__cs_selector = 0;
		env.__opcode = 0;
		env.__data_offset = 0;
		env.__data_selector = 0;
		__asm__ __volatile__("fldenv %0" : : "m"(env));
		set_mxcsr(0x1f80);
		return 0;
	}
	env.__control_word = envp->__control_word;
	env.__status_word = envp->__status_word;
	env.__tags = envp->__tags;
	env.__eip = envp->__eip;
	env.__cs_selector = envp->__cs_selector;
	env.__opcode = envp->__opcode;
	env.__data_offset = envp->__data_offset;
	env.__data_selector = envp->__data_selector;
	__asm__ __volatile__("fldenv %0" : : "m"(env));
	set_mxcsr(envp->__mxcsr);
	return 0;
}

int feupdateenv(const fenv_t *envp)
{
	int raised = fetestexcept(FE_ALL_EXCEPT);
	fesetenv(envp);
	feraiseexcept(raised);
	return 0;
}
