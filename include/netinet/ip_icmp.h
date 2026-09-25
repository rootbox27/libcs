#ifndef _NETINET_IP_ICMP_H
#define _NETINET_IP_ICMP_H
#include <stdint.h>
#include <netinet/ip.h>
struct icmphdr {
	uint8_t type, code;
	uint16_t checksum;
	union {
		struct { uint16_t id, sequence; } echo;
		uint32_t gateway;
		struct { uint16_t __unused, mtu; } frag;
	} un;
};
#define ICMP_ECHOREPLY 0
#define ICMP_DEST_UNREACH 3
#define ICMP_SOURCE_QUENCH 4
#define ICMP_REDIRECT 5
#define ICMP_ECHO 8
#define ICMP_TIME_EXCEEDED 11
#define ICMP_PARAMETERPROB 12
#define ICMP_TIMESTAMP 13
#define ICMP_TIMESTAMPREPLY 14
#define ICMP_NET_UNREACH 0
#define ICMP_HOST_UNREACH 1
#define ICMP_PROT_UNREACH 2
#define ICMP_PORT_UNREACH 3
#define ICMP_FRAG_NEEDED 4
#endif
