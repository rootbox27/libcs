#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
#define MAP_FAILED ((void *)-1)
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_GROWSDOWN 0x0100
#define MAP_POPULATE 0x8000
#define MAP_STACK 0x20000
#define MAP_FIXED_NOREPLACE 0x100000
#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4
#define MCL_CURRENT 1
#define MCL_FUTURE 2
#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8
#define MADV_DONTFORK 10
#define MADV_DOFORK 11
#define MADV_WIPEONFORK 18
#define MADV_KEEPONFORK 19
#define MADV_DONTDUMP 16
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2
#define MFD_CLOEXEC 1U
#define MFD_ALLOW_SEALING 2U
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int msync(void *, size_t, int);
int madvise(void *, size_t, int);
int posix_madvise(void *, size_t, int);
void *mremap(void *, size_t, size_t, int, ...);
int mlock(const void *, size_t);
int munlock(const void *, size_t);
int mlockall(int);
int munlockall(void);
int memfd_create(const char *, unsigned);
int shm_open(const char *, int, mode_t);
int shm_unlink(const char *);
__END_DECLS
#endif
