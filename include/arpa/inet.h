#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#include <features.h>
#include <netinet/in.h>
__BEGIN_DECLS
int inet_pton(int, const char *__restrict, void *__restrict);
const char *inet_ntop(int, const void *__restrict, char *__restrict, socklen_t);
in_addr_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);
char *inet_ntoa(struct in_addr);
__END_DECLS
#endif
