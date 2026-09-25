/* <netdb.h> without the network: numeric hosts, /etc/hosts, services */
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <string.h>
#include "harness.h"

static int count(struct addrinfo *r) { int n = 0; for (; r; r = r->ai_next) n++; return n; }

int main(void)
{
	struct addrinfo hint = { .ai_socktype = SOCK_STREAM }, *r;
	CHECK(getaddrinfo("127.0.0.1", "80", &hint, &r) == 0);
	CHECK(count(r) == 1 && r->ai_family == AF_INET && r->ai_protocol == IPPROTO_TCP);
	struct sockaddr_in *s4 = (void *)r->ai_addr;
	CHECK(ntohs(s4->sin_port) == 80 && s4->sin_addr.s_addr == htonl(0x7f000001));
	freeaddrinfo(r);

	CHECK(getaddrinfo("::1", "https", &hint, &r) == 0);
	struct sockaddr_in6 *s6 = (void *)r->ai_addr;
	CHECK(r->ai_family == AF_INET6 && ntohs(s6->sin6_port) == 443 && IN6_IS_ADDR_LOOPBACK(&s6->sin6_addr));
	freeaddrinfo(r);

	/* no socket type: stream, datagram (and raw without a service) */
	CHECK(getaddrinfo("127.0.0.1", "53", 0, &r) == 0 && count(r) == 2);
	freeaddrinfo(r);
	CHECK(getaddrinfo("127.0.0.1", 0, 0, &r) == 0 && count(r) == 3);
	/* sublists may be freed separately */
	struct addrinfo *rest = r->ai_next;
	r->ai_next = 0;
	freeaddrinfo(rest);
	freeaddrinfo(r);

	/* passive / loopback with no host */
	hint.ai_flags = AI_PASSIVE;
	hint.ai_family = AF_INET;
	CHECK(getaddrinfo(0, "8080", &hint, &r) == 0);
	CHECK(((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr == 0);
	freeaddrinfo(r);
	hint.ai_flags = 0;
	CHECK(getaddrinfo(0, "8080", &hint, &r) == 0);
	CHECK(((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr == htonl(0x7f000001));
	freeaddrinfo(r);

	/* v4-mapped */
	hint.ai_family = AF_INET6;
	CHECK(getaddrinfo("10.1.2.3", "1", &hint, &r) == EAI_ADDRFAMILY);
	hint.ai_flags = AI_V4MAPPED;
	CHECK(getaddrinfo("10.1.2.3", "1", &hint, &r) == 0);
	s6 = (void *)r->ai_addr;
	CHECK(IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr) && s6->sin6_addr.s6_addr[15] == 3);
	freeaddrinfo(r);

	/* localhost comes from /etc/hosts */
	hint = (struct addrinfo){ .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM, .ai_flags = AI_CANONNAME };
	CHECK(getaddrinfo("localhost", "domain", &hint, &r) == 0);
	CHECK(r->ai_canonname && ntohs(((struct sockaddr_in *)r->ai_addr)->sin_port) == 53);
	freeaddrinfo(r);

	/* errors */
	hint = (struct addrinfo){ .ai_socktype = SOCK_STREAM };
	CHECK(getaddrinfo(0, 0, &hint, &r) == EAI_NONAME);
	CHECK(getaddrinfo("127.0.0.1", "no-such-service-x", &hint, &r) == EAI_SERVICE);
	hint.ai_flags = AI_NUMERICSERV;
	CHECK(getaddrinfo("127.0.0.1", "http", &hint, &r) == EAI_NONAME);
	hint.ai_flags = AI_NUMERICHOST;
	CHECK(getaddrinfo("localhost", "80", &hint, &r) == EAI_NONAME);
	hint.ai_flags = 0x10000;
	CHECK(getaddrinfo("127.0.0.1", "80", &hint, &r) == EAI_BADFLAGS);
	hint = (struct addrinfo){ .ai_family = 12345 };
	CHECK(getaddrinfo("127.0.0.1", "80", &hint, &r) == EAI_FAMILY);
	hint = (struct addrinfo){ .ai_socktype = 99 };
	CHECK(getaddrinfo("127.0.0.1", "80", &hint, &r) == EAI_SOCKTYPE);
	hint = (struct addrinfo){ .ai_socktype = SOCK_RAW };
	CHECK(getaddrinfo("127.0.0.1", "80", &hint, &r) == EAI_SERVICE);
	CHECK(getaddrinfo("bad..name", "80", 0, &r) == EAI_NONAME);
	CHECK(getaddrinfo("127.0.0.1", "65536", 0, &r) == EAI_SERVICE);
	CHECK(strlen(gai_strerror(EAI_AGAIN)) > 0);

	/* getnameinfo, numerically */
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(22) };
	inet_pton(AF_INET, "192.0.2.7", &sa.sin_addr);
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	CHECK(getnameinfo((void *)&sa, sizeof sa, host, sizeof host, serv, sizeof serv, NI_NUMERICHOST | NI_NUMERICSERV) == 0);
	CHECK_STR(host, "192.0.2.7");
	CHECK_STR(serv, "22");
	CHECK(getnameinfo((void *)&sa, sizeof sa, 0, 0, serv, sizeof serv, 0) == 0);
	CHECK_STR(serv, "ssh");
	CHECK(getnameinfo((void *)&sa, sizeof sa, host, 4, 0, 0, NI_NUMERICHOST) == EAI_OVERFLOW);
	CHECK(getnameinfo((void *)&sa, 4, host, sizeof host, 0, 0, 0) == EAI_FAMILY);
	struct sockaddr_in6 sa6 = { .sin6_family = AF_INET6, .sin6_scope_id = 1 };
	inet_pton(AF_INET6, "fe80::1", &sa6.sin6_addr);
	CHECK(getnameinfo((void *)&sa6, sizeof sa6, host, sizeof host, 0, 0, NI_NUMERICHOST) == 0);
	CHECK(!strncmp(host, "fe80::1%", 8));
	sa.sin_addr.s_addr = htonl(0x7f000001);
	CHECK(getnameinfo((void *)&sa, sizeof sa, host, sizeof host, 0, 0, NI_NAMEREQD) == 0);
	CHECK(strlen(host) > 0);

	/* services, protocols, interfaces */
	struct servent *se = getservbyname("ssh", "tcp");
	CHECK(se && ntohs(se->s_port) == 22 && !strcmp(se->s_proto, "tcp"));
	se = getservbyport(htons(53), "udp");
	CHECK(se && !strcmp(se->s_name, "domain"));
	CHECK(getprotobyname("tcp")->p_proto == 6 && !strcmp(getprotobynumber(17)->p_name, "udp"));
	unsigned lo = if_nametoindex("lo");
	char ifn[IF_NAMESIZE];
	CHECK(lo > 0 && if_indextoname(lo, ifn) && !strcmp(ifn, "lo"));
	CHECK(if_nametoindex("no-such-if0") == 0);
	struct if_nameindex *ni = if_nameindex();
	CHECK(ni && ni[0].if_index);
	if_freenameindex(ni);

	struct hostent *h = gethostbyname("localhost");
	CHECK(h && h->h_addrtype == AF_INET && h->h_length == 4 && h->h_addr_list[0]);
	h = gethostbyname("1.2.3.4");
	CHECK(h && (unsigned char)h->h_addr_list[0][3] == 4);
	return t_done();
}
