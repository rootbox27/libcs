#ifndef _SETJMP_H
#define _SETJMP_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
/* rbx, rbp, r12-r15, rsp, rip. rbp/rsp/rip are stored mangled with a
 * per-process secret so a leaked or overwritten jmp_buf cannot be used to
 * redirect control flow without knowing the secret. */
typedef struct __jmp_buf_tag {
	unsigned long __jb[8];
	int __mask_was_saved;
	sigset_t __ss;
} jmp_buf[1];
typedef jmp_buf sigjmp_buf;
int setjmp(jmp_buf) __attribute__((__returns_twice__));
__noreturn void longjmp(jmp_buf, int);
int _setjmp(jmp_buf) __attribute__((__returns_twice__));
__noreturn void _longjmp(jmp_buf, int);
int sigsetjmp(sigjmp_buf, int) __attribute__((__returns_twice__));
__noreturn void siglongjmp(sigjmp_buf, int);
__END_DECLS
#endif
