# Citadel libc

A from-scratch, security-focused C library for x86_64 Linux. ISC licensed.

**Status: early work in progress. Not yet compiled or tested.**

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

## Not yet done

- Memory allocator (`malloc` and friends). The plan is to integrate an
  existing permissively licensed hardened allocator.
- stdio/printf, the rest of stdlib, syscall wrappers, signals, time,
  threads, sockets, math, a Makefile and tests.
