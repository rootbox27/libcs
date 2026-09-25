/* getaddrinfo, getnameinfo, the older gethostby* interfaces, services,
 * protocols and interface names. Name resolution itself is in resolv.c. */
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "internal.h"
#include "resolv.h"

/* ---- errors ---- */

const char *gai_strerror(int e)
{
	switch (e) {
	case 0: return "Success";
	case EAI_BADFLAGS: return "Bad value for ai_flags";
	case EAI_NONAME: return "Name or service not known";
	case EAI_AGAIN: return "Temporary failure in name resolution";
	case EAI_FAIL: return "Non-recoverable failure in name resolution";
	case EAI_NODATA: return "No address associated with hostname";
	case EAI_FAMILY: return "ai_family not supported";
	case EAI_SOCKTYPE: return "ai_socktype not supported";
	case EAI_SERVICE: return "Servname not supported for ai_socktype";
	case EAI_ADDRFAMILY: return "Address family for hostname not supported";
	case EAI_MEMORY: return "Memory allocation failure";
	case EAI_SYSTEM: return "System error";
	case EAI_OVERFLOW: return "Result too large for supplied buffer";
	default: return "Unknown error";
	}
}

static _Thread_local int h_err;
int *__h_errno_location(void) { return &h_err; }

const char *hstrerror(int e)
{
	switch (e) {
	case HOST_NOT_FOUND: return "Unknown host";
	case TRY_AGAIN: return "Host name lookup failure";
	case NO_RECOVERY: return "Unknown server error";
	case NO_DATA: return "No address associated with name";
	default: return "Resolver error";
	}
}

void herror(const char *s)
{
	if (s && *s)
		fprintf(stderr, "%s: ", s);
	fprintf(stderr, "%s\n", hstrerror(h_err));
}

/* ---- interfaces ---- */

struct ifreq_min { char name[IF_NAMESIZE]; union { int index; char pad[24]; } u; };

unsigned if_nametoindex(const char *name)
{
	struct ifreq_min r;
	size_t n = strlen(name);
	if (n >= IF_NAMESIZE)
		return 0;
	memset(&r, 0, sizeof r);
	memcpy(r.name, name, n);
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	int ok = ioctl(fd, 0x8933 /* SIOCGIFINDEX */, &r) == 0;
	close(fd);
	return ok ? (unsigned)r.u.index : 0;
}

char *if_indextoname(unsigned idx, char *name)
{
	struct ifreq_min r;
	memset(&r, 0, sizeof r);
	r.u.index = (int)idx;
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	int ok = ioctl(fd, 0x8910 /* SIOCGIFNAME */, &r) == 0;
	close(fd);
	if (!ok) {
		if (errno == ENODEV)
			errno = ENXIO;
		return 0;
	}
	memcpy(name, r.name, IF_NAMESIZE);
	name[IF_NAMESIZE - 1] = 0;
	return name;
}

struct if_nameindex *if_nameindex(void)
{
	DIR *d = opendir("/sys/class/net");
	if (!d)
		return 0;
	size_t n = 0, cap = 8;
	struct if_nameindex *v = malloc(cap * sizeof *v);
	struct dirent *e;
	while (v && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		unsigned idx = if_nametoindex(e->d_name);
		if (!idx)
			continue;
		if (n + 1 >= cap) {
			struct if_nameindex *nv = realloc(v, (cap *= 2) * sizeof *v);
			if (!nv) {
				if_freenameindex(v);
				v = 0;
				break;
			}
			v = nv;
		}
		v[n].if_index = idx;
		if (!(v[n].if_name = strdup(e->d_name))) {
			v[n].if_index = 0;
			if_freenameindex(v);
			v = 0;
			break;
		}
		v[++n] = (struct if_nameindex){ 0, 0 };
	}
	closedir(d);
	if (v && !n)
		v[0] = (struct if_nameindex){ 0, 0 };
	if (!v)
		errno = ENOBUFS;
	return v;
}

void if_freenameindex(struct if_nameindex *v)
{
	for (struct if_nameindex *p = v; p && p->if_index; p++)
		free(p->if_name);
	free(v);
}

/* ---- services and protocols ---- */

static int parse_port(const char *s, int *port)
{
	if (!*s)
		return 0;
	char *e;
	errno = 0;
	unsigned long v = strtoul(s, &e, 10);
	if (*e || errno || v > 65535 || *s == '-' || *s == '+' || *s == ' ')
		return 0;
	*port = (int)v;
	return 1;
}

/* Look through /etc/services: by name (and alias) or by port. proto may
 * be 0 for any. Fills s with storage in buf. */
static struct servent *serv_lookup(const char *name, int port, const char *proto, struct servent *s,
                                   char *buf, size_t blen, char **aliases, int nal)
{
	FILE *f = fopen("/etc/services", "re");
	if (!f)
		return 0;
	char line[512];
	struct servent *r = 0;
	while (!r && fgets(line, sizeof line, f)) {
		line[strcspn(line, "#\n")] = 0;
		char *save, *sname = strtok_r(line, " \t\r", &save);
		char *pp = strtok_r(0, " \t\r", &save);
		if (!sname || !pp)
			continue;
		char *slash = strchr(pp, '/');
		int p;
		if (!slash)
			continue;
		*slash = 0;
		if (!parse_port(pp, &p) || (proto && strcmp(slash + 1, proto)))
			continue;
		int na = 0, hit = port >= 0 ? p == port : !strcmp(sname, name);
		char *al[16];
		for (char *a = strtok_r(0, " \t\r", &save); a; a = strtok_r(0, " \t\r", &save)) {
			if (na < 16)
				al[na++] = a;
			if (port < 0 && !strcmp(a, name))
				hit = 1;
		}
		if (!hit)
			continue;
		/* copy into buf */
		size_t need = strlen(sname) + strlen(slash + 1) + 2;
		for (int i = 0; i < na; i++)
			need += strlen(al[i]) + 1;
		if (need > blen || na >= nal)
			break;
		char *b = buf;
		s->s_name = strcpy(b, sname);
		b += strlen(b) + 1;
		s->s_proto = strcpy(b, slash + 1);
		b += strlen(b) + 1;
		for (int i = 0; i < na; i++) {
			aliases[i] = strcpy(b, al[i]);
			b += strlen(b) + 1;
		}
		aliases[na] = 0;
		s->s_aliases = aliases;
		s->s_port = htons((uint16_t)p);
		r = s;
	}
	fclose(f);
	return r;
}

static struct servent serv_ent;
static char serv_buf[512];
static char *serv_al[17];

struct servent *getservbyname(const char *name, const char *proto)
{
	return serv_lookup(name, -1, proto, &serv_ent, serv_buf, sizeof serv_buf, serv_al, 17);
}

struct servent *getservbyport(int port, const char *proto)
{
	return serv_lookup(0, ntohs((uint16_t)port), proto, &serv_ent, serv_buf, sizeof serv_buf, serv_al, 17);
}

static const struct { int num; const char *name, *alias; } protos[] = {
	{ 0, "ip", "IP" }, { 1, "icmp", "ICMP" }, { 2, "igmp", "IGMP" }, { 6, "tcp", "TCP" },
	{ 17, "udp", "UDP" }, { 41, "ipv6", "IPv6" }, { 47, "gre", "GRE" }, { 50, "esp", "ESP" },
	{ 51, "ah", "AH" }, { 58, "ipv6-icmp", "IPv6-ICMP" }, { 132, "sctp", "SCTP" },
	{ 136, "udplite", "UDPLite" }, { 255, "raw", "RAW" },
};
static struct protoent proto_ent;
static char *proto_al[2];

static struct protoent *proto_at(unsigned i)
{
	proto_ent.p_name = (char *)protos[i].name;
	proto_al[0] = (char *)protos[i].alias;
	proto_al[1] = 0;
	proto_ent.p_aliases = proto_al;
	proto_ent.p_proto = protos[i].num;
	return &proto_ent;
}

struct protoent *getprotobyname(const char *name)
{
	for (unsigned i = 0; i < sizeof protos / sizeof protos[0]; i++)
		if (!strcmp(name, protos[i].name) || !strcmp(name, protos[i].alias))
			return proto_at(i);
	return 0;
}

struct protoent *getprotobynumber(int num)
{
	for (unsigned i = 0; i < sizeof protos / sizeof protos[0]; i++)
		if (protos[i].num == num)
			return proto_at(i);
	return 0;
}

/* ---- getaddrinfo ---- */

struct svc { int socktype, protocol, port; };

/* Resolve the service for the requested socket type(s): up to 3 entries. */
static int services(const char *name, int socktype, int protocol, int flags, struct svc *out, int *count)
{
	int n = 0;
	static const struct { int st, pr; const char *pn; } types[] = {
		{ SOCK_STREAM, IPPROTO_TCP, "tcp" }, { SOCK_DGRAM, IPPROTO_UDP, "udp" }, { SOCK_RAW, 0, 0 },
	};
	int port = 0, numeric = 1;
	if (name && !parse_port(name, &port)) {
		if (flags & AI_NUMERICSERV)
			return EAI_NONAME;
		numeric = 0;
	}
	for (unsigned i = 0; i < 3; i++) {
		if (socktype && socktype != types[i].st)
			continue;
		int pr = types[i].pr;
		if (protocol) {
			if (types[i].st == SOCK_RAW)
				pr = protocol;
			else if (protocol != pr)
				continue;
		}
		if (types[i].st == SOCK_RAW && name)
			continue; /* no ports for raw sockets */
		int p = port;
		if (!numeric) {
			struct servent s;
			char buf[512], *al[17];
			if (!serv_lookup(name, -1, types[i].pn, &s, buf, sizeof buf, al, 17))
				continue;
			p = ntohs((uint16_t)s.s_port);
		}
		out[n++] = (struct svc){ types[i].st, pr, p };
	}
	*count = n;
	if (!n)
		return name && (!numeric || socktype == SOCK_RAW) ? EAI_SERVICE : EAI_SOCKTYPE;
	return 0;
}

/* Whether the system could reach an address of this family (for
 * AI_ADDRCONFIG and for ordering). A UDP connect sends nothing. */
static int reachable(int family, const unsigned char *addr)
{
	static const unsigned char probe6[16] = { 0x20, 0x01, 0x0d, 0xb8, [15] = 1 };
	static const unsigned char probe4[4] = { 192, 0, 2, 1 };
	int fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return 0;
	int r;
	if (family == AF_INET) {
		struct sockaddr_in s = { .sin_family = AF_INET, .sin_port = htons(65535) };
		memcpy(&s.sin_addr, addr ? addr : probe4, 4);
		r = connect(fd, (struct sockaddr *)&s, sizeof s);
	} else {
		struct sockaddr_in6 s = { .sin6_family = AF_INET6, .sin6_port = htons(65535) };
		memcpy(&s.sin6_addr, addr ? addr : probe6, 16);
		r = connect(fd, (struct sockaddr *)&s, sizeof s);
	}
	close(fd);
	return r == 0;
}

struct node {
	struct addrinfo ai;
	union { struct sockaddr_in v4; struct sockaddr_in6 v6; } sa;
};

void freeaddrinfo(struct addrinfo *p)
{
	while (p) {
		struct addrinfo *next = p->ai_next;
		free(p->ai_canonname);
		free(p);
		p = next;
	}
}

static int numeric_host(const char *host, struct dns_addr *a)
{
	struct in_addr v4;
	if (inet_aton(host, &v4)) {
		a->family = AF_INET;
		memcpy(a->addr, &v4, 4);
		a->scope = 0;
		return 1;
	}
	char buf[INET6_ADDRSTRLEN + IF_NAMESIZE + 2];
	size_t n = strlen(host);
	if (n >= sizeof buf)
		return 0;
	memcpy(buf, host, n + 1);
	char *pct = strchr(buf, '%');
	if (pct)
		*pct = 0;
	if (inet_pton(AF_INET6, buf, a->addr) != 1)
		return 0;
	a->family = AF_INET6;
	a->scope = 0;
	if (pct) {
		char *e;
		unsigned long id = strtoul(pct + 1, &e, 10);
		if (*e || !pct[1])
			id = if_nametoindex(pct + 1);
		if (!id || id > UINT_MAX)
			return -1;
		a->scope = (unsigned)id;
	}
	return 1;
}

int getaddrinfo(const char *restrict host, const char *restrict serv, const struct addrinfo *restrict hint,
                struct addrinfo **restrict res)
{
	int flags = hint ? hint->ai_flags : 0;
	int family = hint ? hint->ai_family : AF_UNSPEC;
	int socktype = hint ? hint->ai_socktype : 0;
	int protocol = hint ? hint->ai_protocol : 0;
	*res = 0;
	if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_V4MAPPED | AI_ALL | AI_ADDRCONFIG | AI_NUMERICSERV))
		return EAI_BADFLAGS;
	if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
		return EAI_FAMILY;
	if (socktype && socktype != SOCK_STREAM && socktype != SOCK_DGRAM && socktype != SOCK_RAW)
		return EAI_SOCKTYPE;
	if (!host && !serv)
		return EAI_NONAME;
	if (hint && (hint->ai_addrlen || hint->ai_addr || hint->ai_canonname || hint->ai_next))
		return EAI_BADFLAGS;
	if (!host && (flags & AI_CANONNAME))
		return EAI_BADFLAGS;

	struct svc sv[3];
	int nsv, r = services(serv, socktype, protocol, flags, sv, &nsv);
	if (r)
		return r;

	struct dns_addr *addrs = malloc(MAXADDRS * sizeof *addrs);
	char *canon = malloc(NAME_BUF);
	if (!addrs || !canon) {
		free(addrs);
		free(canon);
		return EAI_MEMORY;
	}
	int n = 0;
	canon[0] = 0;
	int lookup_family = family == AF_INET6 && (flags & AI_V4MAPPED) ? AF_UNSPEC : family;
	if (!host) {
		int v6 = family != AF_INET, v4 = family != AF_INET6;
		if (v4) {
			memset(&addrs[n], 0, sizeof addrs[n]);
			addrs[n].family = AF_INET;
			if (!(flags & AI_PASSIVE)) {
				addrs[n].addr[0] = 127;
				addrs[n].addr[3] = 1;
			}
			n++;
		}
		if (v6) {
			memset(&addrs[n], 0, sizeof addrs[n]);
			addrs[n].family = AF_INET6;
			if (!(flags & AI_PASSIVE))
				addrs[n].addr[15] = 1;
			n++;
		}
	} else {
		int num = numeric_host(host, &addrs[0]);
		if (num < 0) {
			r = EAI_NONAME;
			goto out;
		}
		if (num) {
			n = 1;
			if (family == AF_INET && addrs[0].family == AF_INET6) {
				r = EAI_ADDRFAMILY;
				goto out;
			}
			if (family == AF_INET6 && addrs[0].family == AF_INET && !(flags & AI_V4MAPPED)) {
				r = EAI_ADDRFAMILY;
				goto out;
			}
			strncpy(canon, host, NAME_BUF - 1);
			canon[NAME_BUF - 1] = 0;
		} else if (flags & AI_NUMERICHOST) {
			r = EAI_NONAME;
			goto out;
		} else {
			r = __lookup_name(host, lookup_family, addrs, &n, canon);
			if (r)
				goto out;
		}
	}

	/* AI_ADDRCONFIG: drop families the system has no route for (loopback
	 * addresses stay) */
	if ((flags & AI_ADDRCONFIG) && host) {
		int have4 = reachable(AF_INET, 0), have6 = reachable(AF_INET6, 0);
		int k = 0;
		for (int i = 0; i < n; i++) {
			int loop = addrs[i].family == AF_INET ? addrs[i].addr[0] == 127
			                                      : IN6_IS_ADDR_LOOPBACK((struct in6_addr *)addrs[i].addr);
			if (loop || (addrs[i].family == AF_INET ? have4 : have6))
				addrs[k++] = addrs[i];
		}
		n = k;
		if (!n) {
			r = EAI_NONAME;
			goto out;
		}
	}

	/* AF_INET6 with AI_V4MAPPED: map IPv4 answers, keeping them only
	 * when there are no IPv6 ones (or AI_ALL) */
	if (family == AF_INET6) {
		int have6 = 0, k = 0;
		for (int i = 0; i < n; i++)
			have6 |= addrs[i].family == AF_INET6;
		for (int i = 0; i < n; i++) {
			if (addrs[i].family == AF_INET6) {
				addrs[k++] = addrs[i];
			} else if ((flags & AI_V4MAPPED) && (!have6 || (flags & AI_ALL))) {
				struct dns_addr m = { AF_INET6, { 0 }, 0 };
				m.addr[10] = m.addr[11] = 0xff;
				memcpy(m.addr + 12, addrs[i].addr, 4);
				addrs[k++] = m;
			}
		}
		n = k;
		if (!n) {
			r = EAI_NONAME;
			goto out;
		}
	} else if (family == AF_UNSPEC && n > 1 && host) {
		/* order: reachable addresses first, IPv6 before IPv4 among them */
		int rank[MAXADDRS];
		for (int i = 0; i < n; i++)
			rank[i] = (reachable(addrs[i].family, addrs[i].addr) ? 0 : 2) + (addrs[i].family == AF_INET);
		for (int i = 1; i < n; i++) { /* stable insertion sort */
			struct dns_addr a = addrs[i];
			int ra = rank[i], j = i;
			for (; j > 0 && rank[j - 1] > ra; j--) {
				addrs[j] = addrs[j - 1];
				rank[j] = rank[j - 1];
			}
			addrs[j] = a;
			rank[j] = ra;
		}
	}

	struct addrinfo *head = 0, **tail = &head;
	for (int i = 0; i < n; i++) {
		for (int k = 0; k < nsv; k++) {
			struct node *nd = calloc(1, sizeof *nd);
			if (!nd) {
				freeaddrinfo(head);
				r = EAI_MEMORY;
				goto out;
			}
			nd->ai.ai_family = addrs[i].family;
			nd->ai.ai_socktype = sv[k].socktype;
			nd->ai.ai_protocol = sv[k].protocol;
			nd->ai.ai_addr = (struct sockaddr *)&nd->sa;
			if (addrs[i].family == AF_INET) {
				nd->ai.ai_addrlen = sizeof nd->sa.v4;
				nd->sa.v4.sin_family = AF_INET;
				nd->sa.v4.sin_port = htons((uint16_t)sv[k].port);
				memcpy(&nd->sa.v4.sin_addr, addrs[i].addr, 4);
			} else {
				nd->ai.ai_addrlen = sizeof nd->sa.v6;
				nd->sa.v6.sin6_family = AF_INET6;
				nd->sa.v6.sin6_port = htons((uint16_t)sv[k].port);
				nd->sa.v6.sin6_scope_id = addrs[i].scope;
				memcpy(&nd->sa.v6.sin6_addr, addrs[i].addr, 16);
			}
			if (!head && (flags & AI_CANONNAME)) {
				nd->ai.ai_canonname = strdup(canon[0] ? canon : host);
				if (!nd->ai.ai_canonname) {
					free(nd);
					r = EAI_MEMORY;
					goto out;
				}
			}
			*tail = &nd->ai;
			tail = &nd->ai.ai_next;
		}
	}
	*res = head;
	r = head ? 0 : EAI_NONAME;
out:
	free(addrs);
	free(canon);
	return r;
}

/* ---- getnameinfo ---- */

/* NI_NOFQDN: drop the domain only when it is our own */
static void strip_local_domain(char *name)
{
	char self[256];
	char *dot = strchr(name, '.');
	if (!dot || gethostname(self, sizeof self) < 0)
		return;
	self[sizeof self - 1] = 0;
	char *sd = strchr(self, '.');
	if (sd && !strcasecmp(sd, dot))
		*dot = 0;
}

int getnameinfo(const struct sockaddr *restrict sa, socklen_t salen, char *restrict host, socklen_t hostlen,
                char *restrict serv, socklen_t servlen, int flags)
{
	struct dns_addr a = { 0, { 0 }, 0 };
	int port;
	if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN | NI_NAMEREQD | NI_DGRAM))
		return EAI_BADFLAGS;
	if (!sa)
		return EAI_FAMILY;
	if (sa->sa_family == AF_INET) {
		if (salen < sizeof(struct sockaddr_in))
			return EAI_FAMILY;
		const struct sockaddr_in *s = (const void *)sa;
		a.family = AF_INET;
		memcpy(a.addr, &s->sin_addr, 4);
		port = ntohs(s->sin_port);
	} else if (sa->sa_family == AF_INET6) {
		if (salen < sizeof(struct sockaddr_in6))
			return EAI_FAMILY;
		const struct sockaddr_in6 *s = (const void *)sa;
		a.family = AF_INET6;
		memcpy(a.addr, &s->sin6_addr, 16);
		a.scope = s->sin6_scope_id;
		port = ntohs(s->sin6_port);
	} else {
		return EAI_FAMILY;
	}
	if (!host)
		hostlen = 0;
	if (!serv)
		servlen = 0;
	if (!hostlen && !servlen)
		return EAI_NONAME;

	if (hostlen) {
		char name[NAME_BUF + IF_NAMESIZE + 1];
		int got = 0;
		if (!(flags & NI_NUMERICHOST)) {
			struct dns_addr q = a;
			if (a.family == AF_INET6 && IN6_IS_ADDR_V4MAPPED((struct in6_addr *)a.addr)) {
				q.family = AF_INET;
				memmove(q.addr, a.addr + 12, 4);
			}
			int r = __lookup_addr(&q, name);
			if (r == 0 && __valid_hostname(name)) {
				got = 1;
				if (flags & NI_NOFQDN)
					strip_local_domain(name);
			} else if ((flags & NI_NAMEREQD) && r == EAI_AGAIN) {
				return EAI_AGAIN;
			}
		}
		if (!got) {
			if (flags & NI_NAMEREQD)
				return EAI_NONAME;
			inet_ntop(a.family, a.addr, name, sizeof name);
			if (a.family == AF_INET6 && a.scope) {
				char ifn[IF_NAMESIZE];
				size_t l = strlen(name);
				name[l++] = '%';
				if ((IN6_IS_ADDR_LINKLOCAL((struct in6_addr *)a.addr) ||
				     IN6_IS_ADDR_MC_LINKLOCAL((struct in6_addr *)a.addr)) && if_indextoname(a.scope, ifn))
					strcpy(name + l, ifn);
				else
					snprintf(name + l, sizeof name - l, "%u", a.scope);
			}
		}
		if (strlen(name) >= hostlen)
			return EAI_OVERFLOW;
		strcpy(host, name);
	}
	if (servlen) {
		char buf[32];
		struct servent s;
		char sb[512], *al[17];
		if (!(flags & NI_NUMERICSERV) &&
		    serv_lookup(0, port, (flags & NI_DGRAM) ? "udp" : "tcp", &s, sb, sizeof sb, al, 17))
			snprintf(buf, sizeof buf, "%s", s.s_name);
		else
			snprintf(buf, sizeof buf, "%d", port);
		if (strlen(buf) >= servlen)
			return EAI_OVERFLOW;
		strcpy(serv, buf);
	}
	return 0;
}

/* ---- gethostby* ---- */

/* Fill a hostent in buf from resolved addresses. Returns 0 or ERANGE. */
static int fill_hostent(struct hostent *h, char *buf, size_t blen, const char *name, int family,
                        const struct dns_addr *addrs, int n)
{
	int alen = family == AF_INET ? 4 : 16;
	size_t nl = strlen(name) + 1;
	uintptr_t align = (uintptr_t)buf % sizeof(char *);
	size_t skip = align ? sizeof(char *) - align : 0;
	size_t need = skip + (size_t)(n + 2) * sizeof(char *) + (size_t)n * (size_t)alen + nl;
	if (need > blen)
		return ERANGE;
	char **ptrs = (char **)(buf + skip);
	char *data = (char *)(ptrs + n + 2);
	h->h_addr_list = ptrs;
	h->h_aliases = ptrs + n + 1;
	for (int i = 0; i < n; i++) {
		ptrs[i] = data;
		memcpy(data, addrs[i].addr, (size_t)alen);
		data += alen;
	}
	ptrs[n] = 0;
	ptrs[n + 1] = 0;
	h->h_name = memcpy(data, name, nl);
	h->h_addrtype = family;
	h->h_length = alen;
	return 0;
}

int gethostbyname2_r(const char *name, int family, struct hostent *h, char *buf, size_t blen,
                     struct hostent **res, int *err)
{
	*res = 0;
	if (family != AF_INET && family != AF_INET6) {
		*err = NO_RECOVERY;
		return EAFNOSUPPORT;
	}
	struct dns_addr addrs[MAXADDRS];
	char canon[NAME_BUF];
	int n = 0;
	int num = numeric_host(name, &addrs[0]);
	int r = 0;
	if (num > 0) {
		n = addrs[0].family == family;
		strncpy(canon, name, sizeof canon - 1);
		canon[sizeof canon - 1] = 0;
	} else {
		r = __lookup_name(name, family, addrs, &n, canon);
	}
	if (r || !n) {
		*err = r == EAI_AGAIN ? TRY_AGAIN : r == EAI_NODATA || (!r && !n) ? NO_DATA : HOST_NOT_FOUND;
		return r == EAI_AGAIN ? EAGAIN : ENOENT;
	}
	if (fill_hostent(h, buf, blen, canon[0] ? canon : name, family, addrs, n)) {
		*err = NETDB_INTERNAL;
		return ERANGE;
	}
	*res = h;
	return 0;
}

int gethostbyname_r(const char *name, struct hostent *h, char *buf, size_t blen, struct hostent **res, int *err)
{
	return gethostbyname2_r(name, AF_INET, h, buf, blen, res, err);
}

static struct hostent host_ent;
static char host_buf[MAXADDRS * (16 + sizeof(char *)) + 3 * sizeof(char *) + NAME_BUF + 16];

struct hostent *gethostbyname2(const char *name, int family)
{
	struct hostent *r;
	gethostbyname2_r(name, family, &host_ent, host_buf, sizeof host_buf, &r, &h_err);
	return r;
}

struct hostent *gethostbyname(const char *name)
{
	return gethostbyname2(name, AF_INET);
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int family)
{
	struct dns_addr a = { family, { 0 }, 0 };
	if (!((family == AF_INET && len == 4) || (family == AF_INET6 && len == 16))) {
		h_err = NO_RECOVERY;
		errno = EINVAL;
		return 0;
	}
	memcpy(a.addr, addr, len);
	char name[NAME_BUF];
	int r = __lookup_addr(&a, name);
	if (r || !__valid_hostname(name)) {
		h_err = r == EAI_AGAIN ? TRY_AGAIN : HOST_NOT_FOUND;
		return 0;
	}
	if (fill_hostent(&host_ent, host_buf, sizeof host_buf, name, family, &a, 1))
		return 0;
	return &host_ent;
}
