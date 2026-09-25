#ifndef _SYS_SENDFILE_H
#define _SYS_SENDFILE_H
#include <bits/alltypes.h>
ssize_t sendfile(int, int, off_t *, size_t);
#endif
