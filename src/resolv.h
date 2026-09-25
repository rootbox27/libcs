/* Internal interface between the resolver and the netdb functions. */
#ifndef CITADEL_RESOLV_H
#define CITADEL_RESOLV_H
#include <netinet/in.h>
#include <net/if.h>

#define MAXNS 3
#define MAXADDRS 48
#define NAME_BUF 256
#define ANSZ 65536     /* largest (TCP) answer */
#define ANSZ_UDP 4096

#define T_A 1
#define T_PTR 12
#define T_AAAA 28

#define DNS_NXDOMAIN 1
#define DNS_FAIL 2
#define DNS_NODATA 3

struct dns_addr {
	int family;
	unsigned char addr[16];
	unsigned scope;
};

/* 0 or EAI_*; canon receives the canonical name (NAME_BUF bytes) */
hidden int __lookup_name(const char *name, int family, struct dns_addr *out, int *n, char *canon);
/* 0 with the name in name (NAME_BUF bytes), EAI_NONAME or EAI_AGAIN */
hidden int __lookup_addr(const struct dns_addr *a, char *name);
hidden int __valid_hostname(const char *s);

#endif
