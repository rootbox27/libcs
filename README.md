# Citadel libc

A from-scratch, security-focused C library for x86_64 Linux. ISC licensed.

**Status: early work in progress.** Builds with GCC and Clang; the test
suite passes on x86_64 Linux.

## Building and testing

    make            # lib/libc.a and lib/crt1.o
    make check      # build and run the tests
    make CC=clang check

Each test is linked three ways (static-PIE, static-PIE with RELR packed
relocations, and plain static) against only `crt1.o` and `libc.a`. Tests
named `abort_*` / `segv_*` must die with SIGABRT / SIGSEGV; they cover
the fortify, stack-protector and allocator checks.

## What's here

- `include/` — ~70 headers covering ISO C, POSIX and common Linux APIs
  (sockets, epoll, pthreads, termios), with `_FORTIFY_SOURCE` inline
  wrappers for string, stdio and unistd functions.
- `src/start.c` — process startup: static-PIE self-relocation, RELRO,
  TLS/TCB setup with a random stack canary and pointer-mangling secret,
  and fd 0-2 sanitising for setuid programs.
- `src/arch/` — entry point, `setjmp`/`longjmp` with pointer mangling,
  `clone`, signal return trampoline.
- `src/runtime.c` — errno, futex locks, `abort`, stack-protector and
  fortify failure handlers, `exit`/`atexit` with mangled handler pointers.
- `src/string.c` — string and memory functions, `strlcpy`/`strlcat`,
  `explicit_bzero`, constant-time comparisons, fortify `__*_chk` entry points.
- `src/ctype.c`, `src/syscalls.c` — C-locale ctype; `mmap` family, basic
  fd I/O and process-identity wrappers.
- `src/malloc.c` — **placeholder** allocator (one mapping per allocation,
  end-aligned against a guard page, tagged headers checked on free) so
  the library links and can be tested.

## Not yet done

- Real memory allocator. The plan is to integrate an existing permissively
  licensed hardened allocator in place of `src/malloc.c`.
- stdio/printf, the rest of stdlib, most syscall wrappers, signals, time,
  threads, sockets, math.
