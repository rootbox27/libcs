/* Process entry point. The kernel leaves argc/argv/envp/auxv on the stack. */
	.text
	.globl _start
	.type _start,@function
_start:
	xor %ebp,%ebp
	mov %rsp,%rdi
	and $-16,%rsp
	call __citadel_start_c
	hlt
	.size _start,.-_start
	.section .note.GNU-stack,"",@progbits
