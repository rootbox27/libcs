#ifndef _NETINET_IP_H
#define _NETINET_IP_H
#include <features.h>
#include <stdint.h>
#include <netinet/in.h>
__BEGIN_DECLS
/* x86_64 is little-endian: bit-fields start at the low bits */
struct iphdr {
	unsigned ihl : 4, version : 4;
	uint8_t tos;
	uint16_t tot_len, id, frag_off;
	uint8_t ttl, protocol;
	uint16_t check;
	uint32_t saddr, daddr;
};
struct ip {
	unsigned ip_hl : 4, ip_v : 4;
	uint8_t ip_tos;
	uint16_t ip_len, ip_id, ip_off;
	uint8_t ip_ttl, ip_p;
	uint16_t ip_sum;
	struct in_addr ip_src, ip_dst;
};
#define IPVERSION 4
#define IP_MAXPACKET 65535
#define IP_RF 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
#define IPTOS_LOWDELAY 0x10
#define IPTOS_THROUGHPUT 0x08
#define IPTOS_RELIABILITY 0x04
#define IPDEFTTL 64
#define MAXTTL 255
__END_DECLS
#endif
