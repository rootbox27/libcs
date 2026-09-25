/* Citadel libc internal definitions. Not installed. */
#ifndef CITADEL_INTERNAL_H
#define CITADEL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <bits/alltypes.h>

#define hidden __attribute__((__visibility__("hidden")))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#define strong_alias(old, new) extern __typeof(old) new __attribute__((__alias__(#old)))

#define PAGE_SZ 4096UL
#define ROUND_UP(x, a) (((x) + (a) - 1) & ~((uintptr_t)(a) - 1))
#define ROUND_DOWN(x, a) ((x) & ~((uintptr_t)(a) - 1))

/* ---- raw system calls --------------------------------------------------- */

static inline long __syscall0(long n)
{
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall1(long n, long a)
{
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall2(long n, long a, long b)
{
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall3(long n, long a, long b, long c)
{
	long r;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall4(long n, long a, long b, long c, long d)
{
	long r;
	register long r10 __asm__("r10") = d;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
	long r;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
	return r;
}
static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
	long r;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	__asm__ __volatile__("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return r;
}

#define __SC_NARGS(...) __SC_NARGS_(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0)
#define __SC_NARGS_(a, b, c, d, e, f, g, n, ...) n
#define __SC_CAT(a, b) __SC_CAT_(a, b)
#define __SC_CAT_(a, b) a##b
#define __sysc_1(n) __syscall0(n)
#define __sysc_2(n, a) __syscall1(n, (long)(a))
#define __sysc_3(n, a, b) __syscall2(n, (long)(a), (long)(b))
#define __sysc_4(n, a, b, c) __syscall3(n, (long)(a), (long)(b), (long)(c))
#define __sysc_5(n, a, b, c, d) __syscall4(n, (long)(a), (long)(b), (long)(c), (long)(d))
#define __sysc_6(n, a, b, c, d, e) __syscall5(n, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e))
#define __sysc_7(n, a, b, c, d, e, f) __syscall6(n, (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))
/* __sys(SYS_x, args...) -> raw kernel return (negative errno on error) */
#define __sys(...) __SC_CAT(__sysc_, __SC_NARGS(__VA_ARGS__))(__VA_ARGS__)

hidden long __syscall_ret(unsigned long r);
/* sys(SYS_x, args...) -> libc convention: -1 and errno on error */
#define sys(...) __syscall_ret(__sys(__VA_ARGS__))

/* ---- thread control block ---------------------------------------------- */
/* %fs points at this structure. Offsets 0x00 (self) and 0x28 (stack
 * protector canary) are fixed by the x86_64 ABI used by GCC and Clang. */
struct pthread {
	struct pthread *self;        /* 0x00 */
	void *dtv;                   /* 0x08 */
	void *__pad0[3];             /* 0x10 - 0x27 */
	uintptr_t canary;            /* 0x28 */
	uintptr_t ptr_guard;         /* 0x30: pointer-mangling secret (read by asm) */
	/* private fields below */
	int tid;
	int errno_val;
	int detached;                /* 0 joinable, 1 detached, 2 exited-detached */
	volatile int exit_futex;     /* cleared by kernel (CLONE_CHILD_CLEARTID) */
	void *(*start)(void *);
	void *arg;
	void *result;
	void *map_base;              /* whole mapping: guard + stack + tls + tcb */
	size_t map_size;
	void **tsd;                  /* thread-specific data array */
	int cancel_disabled;
	int cancel_async;
	volatile int canceled;
	struct pthread *next, *prev; /* list of live threads */
	char name[16];
	unsigned long sigmask_saved;
	void *tsd_storage[128];
};

static inline struct pthread *__self(void)
{
	struct pthread *p;
	__asm__("mov %%fs:0, %0" : "=r"(p));
	return p;
}

/* ---- global runtime state ---------------------------------------------- */
struct libc_state {
	size_t *auxv;
	int secure;             /* AT_SECURE: setuid/setgid or capabilities */
	int threaded;           /* set once a second thread has been created */
	size_t tls_size, tls_align, tls_file_size, tls_offset;
	const void *tls_image;
	uintptr_t ptr_guard;    /* secret for pointer mangling */
	uintptr_t canary;
	const char *progname;
	volatile int thread_list_lock;
};
extern hidden struct libc_state __libc;
extern char **__environ;

static inline uintptr_t __ptr_mangle(uintptr_t p)
{
	p ^= __libc.ptr_guard;
	return (p << 17) | (p >> (64 - 17));
}
static inline uintptr_t __ptr_demangle(uintptr_t p)
{
	p = (p >> 17) | (p << (64 - 17));
	return p ^ __libc.ptr_guard;
}

/* ---- locking ----------------------------------------------------------- */
hidden void __lock(volatile int *);
hidden void __unlock(volatile int *);
hidden int __futex_wait(volatile int *addr, int val, const struct timespec *ts);
hidden int __futex_wake(volatile int *addr, int n);
#define LOCK(l) (__libc.threaded ? __lock(&(l)) : (void)0)
#define UNLOCK(l) (__libc.threaded ? __unlock(&(l)) : (void)0)

/* ---- misc internal helpers --------------------------------------------- */
hidden __attribute__((__noreturn__, __cold__)) void __fatal(const char *msg);
hidden __attribute__((__noreturn__, __cold__)) void __chk_fail(void);
hidden void __write_str(int fd, const char *s);
hidden void __stdio_exit(void);
hidden void __malloc_threads_start(void);
hidden void __random_fork(void);
hidden void __malloc_atfork(int phase);
hidden void __stdio_atfork(int phase);
hidden void __tsd_run_dtors(void);
hidden void __testcancel(void);
hidden int __futex_timedwait(volatile int *addr, int val, clockid_t clk, const struct timespec *abs, int priv);
hidden int __clone(int (*fn)(void *), void *stack, int flags, void *arg, int *ptid, void *tls, int *ctid);
hidden __attribute__((__noreturn__)) void __unmapself(void *base, size_t size);
hidden int __fmodeflags(const char *mode);
hidden void __init_tp(struct pthread *p);
hidden void *__mmap_raw(size_t len);
hidden void __secure_random(void *buf, size_t len);

struct k_sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned mask[2];
};
hidden void __restore_rt(void);


#endif
