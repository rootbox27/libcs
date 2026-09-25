# Citadel libc

A from-scratch, security-focused C library for x86_64 Linux. ISC licensed.

**Status: work in progress.** Enough of ISO C and POSIX is implemented to
build and run ordinary command-line programs statically; see *Not yet
done* for the gaps. Builds with GCC and Clang, and the test suite passes
with both.

## Building and testing

    make            # lib/libc.a and lib/crt1.o
    make check      # build and run the tests
    make CC=clang check

Programs link statically against `crt1.o` and `libc.a` only:

    cc -nostdinc -isystem include -isystem $(cc -print-file-name=include) \
       -nostdlib -static-pie -o prog lib/crt1.o prog.c lib/libc.a $(cc -print-libgcc-file-name)

Each test is linked three ways (static-PIE, static-PIE with RELR packed
relocations, and plain static). Tests named `abort_*` / `segv_*` must die
with SIGABRT / SIGSEGV; they cover the fortify, stack-protector and
allocator checks. A test may have a `.expected` file for its stdout.

## Hardening

- Stack protector canary and a pointer-mangling secret, both random per
  process, set up before any C code that could use them.
- Function pointers the library stores are mangled: `atexit` handlers,
  FILE callbacks, thread-specific-data destructors, `setjmp` buffers.
- `_FORTIFY_SOURCE` wrappers for string, stdio and unistd functions;
  bounds checks on `FD_SET` and friends.
- `printf` aborts on `%n`.
- Static-PIE self-relocation with full RELRO; fds 0-2 are opened on
  `/dev/null` if closed at startup of a setuid program.
- `arc4random` (ChaCha20) with its state wiped on fork and excluded from
  core dumps.
- Allocator (placeholder, see below) puts each allocation against a guard
  page and checks a tagged header on `free`.
- `setuid` and the other set*id calls apply to every thread (a signal
  broadcast, as the kernel only changes the calling thread); if any thread
  fails to switch, the process aborts rather than run with mixed
  credentials.
- `regex`: a Pike VM with a linear-time guarantee. Only back-references
  need backtracking, and that is capped by a step budget (`REG_ESPACE`).
- DNS: the response parser bounds-checks everything, accepts only
  backward compression pointers and restricts names to hostname
  characters.
- `wordexp` expands in-process. Only command substitution runs a shell,
  and with `WRDE_NOCMD` any substitution anywhere in the word (including
  in `${x:-...}`) is refused before anything is expanded. Nesting depth is
  bounded throughout.
- `crypt` supports only SHA-crypt and bcrypt, returns `*0`/`*1` on
  failure, never a weak hash, and wipes its intermediate state.
  `hsearch` hashes with keyed SipHash.

## What's here

- **Startup and runtime** (`src/start.c`, `src/runtime.c`, `src/arch/`):
  static-PIE relocation (including RELR), TLS, `exit`/`atexit`, errno,
  futex-based locks, `setjmp`/`longjmp`, `clone`.
- **Strings and memory**: `<string.h>`, `<strings.h>`, `strlcpy`/`strlcat`,
  `explicit_bzero`, constant-time comparisons.
- **stdio**: buffered FILE streams, `printf`/`scanf` families (including
  wide-character versions), `fmemopen`, `open_memstream`, `popen`,
  `getline`, temporary files. `strtod` and `printf` are correctly rounded.
- **stdlib**: conversions, `qsort`, environment, `realpath`, `mkstemp`
  family, `arc4random`, random numbers.
- **Multibyte and wide characters**: UTF-8 conversions, `<wchar.h>`,
  `<wctype.h>` with Unicode 15 tables (`scripts/gen-unicode.pl`).
  `setlocale` accepts only the C and C.UTF-8 locales.
- **POSIX**: file system and fd wrappers, directories, signals, process
  control (`fork`, `exec*`, `wait*`), time zones (TZif files and
  POSIX TZ strings), `strftime`/`strptime`, termios, sockets and
  `inet_*`, `poll`/`select`/`epoll`, `getopt`/`getopt_long`, `fnmatch`,
  `passwd`/`group` lookups, `syslog`, `err`/`warn`.
- **Threads**: `<pthread.h>` — threads, mutexes, condition variables,
  rwlocks, barriers, spinlocks, once, thread-specific data and
  cancellation; C11 `<threads.h>`; `<semaphore.h>` (named semaphores in
  `/dev/shm`); POSIX timers with all notification kinds; `<mqueue.h>`;
  `<aio.h>` on worker threads; `<ucontext.h>`.
- **Processes**: `<spawn.h>` (`posix_spawn` via `CLONE_VM|CLONE_VFORK`),
  `<pty.h>` and `posix_openpt`.
- **Networking**: `<netdb.h>` — `getaddrinfo`/`getnameinfo` with the
  hosts file and a DNS stub resolver, the `gethostby*`, `getserv*`,
  `getproto*` families; `<ifaddrs.h>`, `<net/if.h>`, packet headers.
- **Text**: `<regex.h>` (POSIX basic and extended, matching glibc on a
  1,800-case corpus), `<glob.h>` (with GNU brace and tilde expansion),
  `<wordexp.h>`, `<iconv.h>` (UTF-8/16/32, UCS-2/4, ASCII, ISO-8859-1/15
  and CP1252,
  matching glibc output), `<monetary.h>`, `<fmtmsg.h>`, `<langinfo.h>`,
  `<nl_types.h>`.
- **Other POSIX and Linux interfaces**: `<search.h>`, `<ftw.h>`,
  `<crypt.h>`, `<shadow.h>`, `<utmpx.h>`, `<mntent.h>`, `<uchar.h>`,
  `<dlfcn.h>` (static only), `<execinfo.h>`, System V IPC, and the Linux
  timerfd, signalfd, inotify, xattr, mount and statfs wrappers.
- **Math** (`src/math/`): all of C99 `<math.h>` in double, float and long
  double, plus `<fenv.h>`.
  - The double functions use double-double kernels with tables and
    minimax polynomials generated by `scripts/gen-math.py` (mpmath).
    Measured against mpmath, almost all stay within 0.52 ulp; tan
    reaches 0.54. glibc is 1-5 ulp on several of them.
  - float functions are evaluated in double and rounded once.
  - long double: table-driven double-long-double exp and log kernels,
    so `expl`, `logl`, `powl`, the hyperbolic functions and `cbrtl`
    round correctly in nearly all cases. `sinl`, `cosl` and `tanl` do
    their own argument reduction (Payne-Hanek for large arguments) and
    then use the x87 instructions (about 1 ulp), as does `atanl`.
  - `fma`, `fmaf` and `fmal` are exact and, with `remquo`, match glibc
    bit for bit, including exception flags, in all rounding modes.
  - Known weak spot: `lgamma` loses relative accuracy right next to its
    zeros at negative arguments (up to about 8 ulp near -2.457).
  - `<complex.h>`: all C99 functions with the Annex G special values.
    The long double versions follow Hull, Fairgrieve and Tang for the
    inverse functions, and use double-long-double sums of squares so
    nothing cancels near the unit circle (at most about 3 ulp, for ctan
    and ctanh). The double versions evaluate in long double and measure
    at most 0.5 ulp per component; glibc reaches 3 ulp on the same points.
  - `<tgmath.h>` covers both real and complex arguments.

## Not yet done

- A real memory allocator. `src/malloc.c` is a placeholder that makes
  one mapping per allocation; it is safe but slow and wasteful.
- Locales other than C/C.UTF-8.
- Legacy password hashes (DES and MD5 `crypt`) are refused on purpose.
- Dynamic linking: only static executables are supported.
- Architectures other than x86_64.
