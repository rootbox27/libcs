#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
#define GRND_NONBLOCK 1
#define GRND_RANDOM 2
#define GRND_INSECURE 4
ssize_t getrandom(void *, size_t, unsigned);
int getentropy(void *, size_t);
__END_DECLS
#endif
