#ifndef _NETINET_UDP_H
#define _NETINET_UDP_H
#include <stdint.h>
struct udphdr {
	union {
		struct { uint16_t uh_sport, uh_dport, uh_ulen, uh_sum; };
		struct { uint16_t source, dest, len, check; };
	};
};
#define SOL_UDP 17
#define UDP_CORK 1
#define UDP_ENCAP 100
#define UDP_SEGMENT 103
#define UDP_GRO 104
#endif
