/* Entry point of libc.so when the kernel starts a program that names it
 * as its interpreter. __dls_relocate_self applies libc's relative
 * relocations; __dls_start (a separate call, see dynlink.c) loads and
 * binds everything, runs the constructors and returns the program's entry
 * point, which is entered with the original stack. */
	.text
	.globl _dlstart
	.hidden _dlstart
	.type _dlstart,@function
_dlstart:
	xor %ebp,%ebp
	mov %rsp,%rbx
	and $-16,%rsp
	mov %rbx,%rdi
	call __dls_relocate_self
	mov %rbx,%rdi
	call __dls_start
	mov %rbx,%rsp
	xor %edx,%edx   /* no rtld_fini: exit() runs every destructor */
	jmp *%rax
	.size _dlstart,.-_dlstart
	.section .note.GNU-stack,"",@progbits
