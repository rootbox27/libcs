/* makecontext; the rest of <ucontext.h> is in arch/ucontext.S. */
#include <ucontext.h>
#include <stdarg.h>
#include <stdint.h>
#include "internal.h"

hidden void __start_context(void);

void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...)
{
	if (argc < 0)
		argc = 0;
	uintptr_t top = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
	int nstack = argc > 6 ? argc - 6 : 0;
	/* at entry, as after a call: rsp + 8 is 16-byte aligned */
	unsigned long long *sp = (unsigned long long *)(((top - (size_t)nstack * 8) & ~(uintptr_t)15) - 8);
	if ((uintptr_t)sp < (uintptr_t)ucp->uc_stack.ss_sp)
		__fatal("makecontext: arguments do not fit on the context's stack");
	sp[0] = (uintptr_t)__start_context;
	greg_t *g = ucp->uc_mcontext.gregs;
	g[REG_RIP] = (greg_t)(uintptr_t)func;
	g[REG_RSP] = (greg_t)(uintptr_t)sp;
	g[REG_RBX] = (greg_t)(uintptr_t)ucp->uc_link;
	static const int regs[6] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
	va_list ap;
	va_start(ap, argc);
	for (int i = 0; i < argc; i++) {
		greg_t v = va_arg(ap, greg_t);
		if (i < 6)
			g[regs[i]] = v;
		else
			sp[i - 5] = (unsigned long long)v;
	}
	va_end(ap);
}
