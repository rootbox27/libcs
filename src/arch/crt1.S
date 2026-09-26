/* Process entry point. The kernel (or, for a dynamically linked program,
 * the dynamic linker) leaves argc/argv/envp/auxv on the stack. main is
 * passed along so that libc.so, which cannot refer to it, can call it. */
	.text
	.globl _start
	.type _start,@function
_start:
	xor %ebp,%ebp
	mov %rsp,%rdi
	mov main@GOTPCREL(%rip),%rsi
	and $-16,%rsp
	call __citadel_start_c@PLT
	hlt
	.size _start,.-_start
	.section .note.GNU-stack,"",@progbits
