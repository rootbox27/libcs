#ifndef _UCONTEXT_H
#define _UCONTEXT_H
#include <sys/ucontext.h>
__BEGIN_DECLS
int getcontext(ucontext_t *);
int setcontext(const ucontext_t *);
void makecontext(ucontext_t *, void (*)(void), int, ...);
int swapcontext(ucontext_t *__restrict, const ucontext_t *__restrict);
__END_DECLS
#endif
