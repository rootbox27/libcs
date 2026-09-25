#ifndef _BITS_FORTIFY_UNISTD_H
#define _BITS_FORTIFY_UNISTD_H
__BEGIN_DECLS
ssize_t __read_chk(int, void *, size_t, size_t);
ssize_t __pread_chk(int, void *, size_t, off_t, size_t);
char *__getcwd_chk(char *, size_t, size_t);
ssize_t __readlink_chk(const char *, char *, size_t, size_t);
__fortify_inline ssize_t read(int fd, void *b, size_t n) { return __read_chk(fd, b, n, __bos0(b)); }
__fortify_inline ssize_t pread(int fd, void *b, size_t n, off_t o) { return __pread_chk(fd, b, n, o, __bos0(b)); }
__fortify_inline char *getcwd(char *b, size_t n) { return __getcwd_chk(b, n, __bos(b)); }
__fortify_inline ssize_t readlink(const char *__restrict p, char *__restrict b, size_t n) { return __readlink_chk(p, b, n, __bos(b)); }
__END_DECLS
#endif
