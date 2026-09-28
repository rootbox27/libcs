/* Entry point of libc.so when the kernel starts a program that names it
 * as its interpreter (or libc.so itself). __dls_relocate_self applies
 * libc's relative relocations; __dls_start (a separate call, see
 * dynlink.c) loads and binds everything, runs the constructors and
 * returns the program's entry point. It is given the address of the
 * saved stack pointer, which it moves when it drops its own arguments
 * (libc.so run as a program); the program is entered with that stack. */
	.text
	.globl _dlstart
	.hidden _dlstart
	.type _dlstart,@function
_dlstart:
	xor %ebp,%ebp
	mov %rsp,%rdi
	and $-16,%rsp
	push %rdi
	push %rdi       /* (%rsp): the stack pointer to enter the program with */
	call __dls_relocate_self
	mov %rsp,%rdi
	call __dls_start
	mov (%rsp),%rsp
	xor %edx,%edx   /* no rtld_fini: exit() runs every destructor */
	jmp *%rax
	.size _dlstart,.-_dlstart

/* TLS descriptors: every module's block is static, so the descriptor's
 * second word is already the offset from the thread pointer. Only %rax
 * may change. */
	.globl __tlsdesc_static
	.hidden __tlsdesc_static
	.type __tlsdesc_static,@function
__tlsdesc_static:
	mov 8(%rax),%rax
	ret
	.size __tlsdesc_static,.-__tlsdesc_static
	.section .note.GNU-stack,"",@progbits
