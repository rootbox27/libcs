/* int __clone(fn, stack, flags, arg, ptid, tls, ctid) */
	.text
	.globl __clone
	.hidden __clone
	.type __clone,@function
__clone:
	xor %eax,%eax
	mov $56,%al
	mov %rdi,%r11
	mov %rdx,%rdi
	mov %r8,%rdx
	mov %r9,%r8
	mov 8(%rsp),%r10
	mov %r11,%r9
	and $-16,%rsi
	sub $8,%rsi
	mov %rcx,(%rsi)
	syscall
	test %eax,%eax
	jnz 1f
	xor %ebp,%ebp
	pop %rdi
	call *%r9
	mov %eax,%edi
	mov $60,%eax
	syscall
	hlt
1:	ret
	.size __clone,.-__clone

/* Unmap the calling thread's own stack and exit, without using the stack. */
	.globl __unmapself
	.hidden __unmapself
	.type __unmapself,@function
__unmapself:
	mov $11,%eax
	syscall
	xor %edi,%edi
	mov $60,%eax
	syscall
	hlt
	.size __unmapself,.-__unmapself

	.globl __restore_rt
	.hidden __restore_rt
	.type __restore_rt,@function
__restore_rt:
	mov $15,%eax
	syscall
	hlt
	.size __restore_rt,.-__restore_rt
	.section .note.GNU-stack,"",@progbits
