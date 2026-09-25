/* setjmp/longjmp with pointer mangling.
 * jmp_buf: [0]rbx [1]rbp* [2]r12 [3]r13 [4]r14 [5]r15 [6]rsp* [7]rip*
 * (* = XORed with the per-process secret at %fs:0x30 and rotated left 17). */
	.text
	.globl setjmp, _setjmp, __setjmp_int
	.type setjmp,@function
	.type _setjmp,@function
setjmp:
_setjmp:
__setjmp_int:
	mov %rbx,(%rdi)
	mov %rbp,%rax
	xor %fs:0x30,%rax
	rol $17,%rax
	mov %rax,8(%rdi)
	mov %r12,16(%rdi)
	mov %r13,24(%rdi)
	mov %r14,32(%rdi)
	mov %r15,40(%rdi)
	lea 8(%rsp),%rax
	xor %fs:0x30,%rax
	rol $17,%rax
	mov %rax,48(%rdi)
	mov (%rsp),%rax
	xor %fs:0x30,%rax
	rol $17,%rax
	mov %rax,56(%rdi)
	xor %eax,%eax
	ret
	.size setjmp,.-setjmp

	.globl longjmp, _longjmp
	.type longjmp,@function
	.type _longjmp,@function
longjmp:
_longjmp:
	mov %esi,%eax
	test %eax,%eax
	jnz 1f
	inc %eax
1:	mov (%rdi),%rbx
	mov 8(%rdi),%rdx
	ror $17,%rdx
	xor %fs:0x30,%rdx
	mov %rdx,%rbp
	mov 16(%rdi),%r12
	mov 24(%rdi),%r13
	mov 32(%rdi),%r14
	mov 40(%rdi),%r15
	mov 48(%rdi),%rdx
	ror $17,%rdx
	xor %fs:0x30,%rdx
	mov %rdx,%rsp
	mov 56(%rdi),%rdx
	ror $17,%rdx
	xor %fs:0x30,%rdx
	jmp *%rdx
	.size longjmp,.-longjmp

/* sigsetjmp(buf, savemask): optionally save the signal mask, then setjmp.
 * Done without touching the stack so setjmp sees our caller's frame. */
	.globl sigsetjmp, __sigsetjmp
	.type sigsetjmp,@function
sigsetjmp:
__sigsetjmp:
	mov %esi,64(%rdi)
	test %esi,%esi
	jz setjmp
	mov %rdi,%r8
	lea 72(%rdi),%rdx
	xor %esi,%esi
	xor %edi,%edi
	mov $8,%r10d
	mov $14,%eax
	syscall
	mov %r8,%rdi
	jmp setjmp
	.size sigsetjmp,.-sigsetjmp
	.section .note.GNU-stack,"",@progbits
