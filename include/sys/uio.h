#ifndef _SYS_UIO_H
#define _SYS_UIO_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);
ssize_t preadv(int, const struct iovec *, int, off_t);
ssize_t pwritev(int, const struct iovec *, int, off_t);
__END_DECLS
#endif
