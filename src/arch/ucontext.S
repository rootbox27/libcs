/* getcontext/setcontext/swapcontext/makecontext for x86_64.
 * Offsets into ucontext_t (see include/sys/ucontext.h): */
#define oRBP   120
#define oRSP   160
#define oRBX   128
#define oR8    40
#define oR9    48
#define oR12   72
#define oR13   80
#define oR14   88
#define oR15   96
#define oRDI   104
#define oRSI   112
#define oRDX   136
#define oRCX   152
#define oRIP   168
#define oFPREGS 224
#define oSIGMASK 296
#define oFPMEM 424
#define oMXCSR (oFPMEM + 24)

#define SYS_rt_sigprocmask 14
#define SIG_BLOCK 0
#define SIG_SETMASK 2

/* store the callee-saved state and our caller's rsp/rip into (%rdi) */
.macro SAVE
	mov %rbx,oRBX(%rdi)
	mov %rbp,oRBP(%rdi)
	mov %r12,oR12(%rdi)
	mov %r13,oR13(%rdi)
	mov %r14,oR14(%rdi)
	mov %r15,oR15(%rdi)
	mov %rdi,oRDI(%rdi)
	mov %rsi,oRSI(%rdi)
	mov %rdx,oRDX(%rdi)
	mov %rcx,oRCX(%rdi)
	mov %r8,oR8(%rdi)
	mov %r9,oR9(%rdi)
	mov (%rsp),%rcx
	mov %rcx,oRIP(%rdi)
	lea 8(%rsp),%rcx
	mov %rcx,oRSP(%rdi)
	lea oFPMEM(%rdi),%rcx
	mov %rcx,oFPREGS(%rdi)
	fnstenv (%rcx)
	fldenv (%rcx)
	stmxcsr oMXCSR(%rdi)
.endm

/* load the context at (%rdx) and jump to it, returning 0 */
.macro LOAD
	mov oFPREGS(%rdx),%rcx
	fldenv (%rcx)
	ldmxcsr oMXCSR(%rdx)
	mov oRSP(%rdx),%rsp
	mov oRBX(%rdx),%rbx
	mov oRBP(%rdx),%rbp
	mov oR12(%rdx),%r12
	mov oR13(%rdx),%r13
	mov oR14(%rdx),%r14
	mov oR15(%rdx),%r15
	push oRIP(%rdx)
	mov oRDI(%rdx),%rdi
	mov oRSI(%rdx),%rsi
	mov oRCX(%rdx),%rcx
	mov oR8(%rdx),%r8
	mov oR9(%rdx),%r9
	mov oRDX(%rdx),%rdx
	xor %eax,%eax
	ret
.endm

	.text
	.globl getcontext
	.type getcontext,@function
getcontext:
	SAVE
	lea oSIGMASK(%rdi),%rdx
	xor %esi,%esi
	mov $SIG_BLOCK,%edi
	mov $8,%r10d
	mov $SYS_rt_sigprocmask,%eax
	syscall
	cmp $-4095,%rax
	jae .Lerr
	xor %eax,%eax
	ret
	.size getcontext,.-getcontext

	.globl setcontext
	.type setcontext,@function
setcontext:
	push %rdi
	lea oSIGMASK(%rdi),%rsi
	xor %edx,%edx
	mov $SIG_SETMASK,%edi
	mov $8,%r10d
	mov $SYS_rt_sigprocmask,%eax
	syscall
	pop %rdx
	cmp $-4095,%rax
	jae .Lerr
	LOAD
	.size setcontext,.-setcontext

	.globl swapcontext
	.type swapcontext,@function
swapcontext:
	SAVE
	mov %rsi,%r8
	mov %rdi,%r9
	lea oSIGMASK(%rdi),%rdx
	lea oSIGMASK(%rsi),%rsi
	mov $SIG_SETMASK,%edi
	mov $8,%r10d
	mov $SYS_rt_sigprocmask,%eax
	syscall
	cmp $-4095,%rax
	jae .Lerr
	/* the saved context resumes as a successful return */
	mov %r8,%rdx
	LOAD
	.size swapcontext,.-swapcontext

.Lerr:
	mov %rax,%rdi
	jmp __syscall_ret

/* A context made by makecontext returns here with %rbx = uc_link. */
	.globl __start_context
	.hidden __start_context
	.type __start_context,@function
__start_context:
	test %rbx,%rbx
	jz 1f
	mov %rbx,%rdi
	call setcontext
	/* the successor context was unusable: do not carry on blindly */
	lea .Lmsg(%rip),%rdi
	call __fatal
1:	xor %edi,%edi
	call exit
	hlt
	.size __start_context,.-__start_context

	.section .rodata
.Lmsg:	.asciz "setcontext: uc_link context could not be resumed"
	.section .note.GNU-stack,"",@progbits
