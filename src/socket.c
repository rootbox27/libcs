/* Sockets and IPv4/IPv6 address conversion. */
#include "internal.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int socket(int dom, int type, int proto) { return (int)sys(SYS_socket, dom, type, proto); }
int socketpair(int dom, int type, int proto, int fd[2]) { return (int)sys(SYS_socketpair, dom, type, proto, fd); }
int bind(int fd, const struct sockaddr *a, socklen_t l) { return (int)sys(SYS_bind, fd, a, l); }
int connect(int fd, const struct sockaddr *a, socklen_t l) { return (int)sys(SYS_connect, fd, a, l); }
int listen(int fd, int backlog) { return (int)sys(SYS_listen, fd, backlog); }
int accept4(int fd, struct sockaddr *__restrict a, socklen_t *__restrict l, int flags) { return (int)sys(SYS_accept4, fd, a, l, flags); }
int accept(int fd, struct sockaddr *__restrict a, socklen_t *__restrict l) { return accept4(fd, a, l, 0); }
int shutdown(int fd, int how) { return (int)sys(SYS_shutdown, fd, how); }
int getsockname(int fd, struct sockaddr *__restrict a, socklen_t *__restrict l) { return (int)sys(SYS_getsockname, fd, a, l); }
int getpeername(int fd, struct sockaddr *__restrict a, socklen_t *__restrict l) { return (int)sys(SYS_getpeername, fd, a, l); }
int getsockopt(int fd, int lvl, int opt, void *__restrict v, socklen_t *__restrict l) { return (int)sys(SYS_getsockopt, fd, lvl, opt, v, l); }
int setsockopt(int fd, int lvl, int opt, const void *v, socklen_t l) { return (int)sys(SYS_setsockopt, fd, lvl, opt, v, l); }
ssize_t sendto(int fd, const void *b, size_t n, int fl, const struct sockaddr *a, socklen_t l) { return sys(SYS_sendto, fd, b, n, fl, a, l); }
ssize_t recvfrom(int fd, void *__restrict b, size_t n, int fl, struct sockaddr *__restrict a, socklen_t *__restrict l)
{
	return sys(SYS_recvfrom, fd, b, n, fl, a, l);
}
ssize_t send(int fd, const void *b, size_t n, int fl) { return sendto(fd, b, n, fl, 0, 0); }
ssize_t recv(int fd, void *b, size_t n, int fl) { return recvfrom(fd, b, n, fl, 0, 0); }
ssize_t sendmsg(int fd, const struct msghdr *m, int fl) { return sys(SYS_sendmsg, fd, m, fl); }
ssize_t recvmsg(int fd, struct msghdr *m, int fl) { return sys(SYS_recvmsg, fd, m, fl); }

struct cmsghdr *__cmsg_nxthdr(struct msghdr *m, struct cmsghdr *c)
{
	if (c->cmsg_len < sizeof *c)
		return 0;
	unsigned char *end = (unsigned char *)m->msg_control + m->msg_controllen;
	unsigned char *next = (unsigned char *)c + CMSG_ALIGN(c->cmsg_len);
	/* the next header must fit completely, including its data */
	if (next < (unsigned char *)c || (size_t)(end - next) < sizeof(struct cmsghdr) ||
	    ((struct cmsghdr *)next)->cmsg_len > (size_t)(end - next))
		return 0;
	return (struct cmsghdr *)next;
}

uint16_t (htons)(uint16_t x) { return __builtin_bswap16(x); }
uint16_t (ntohs)(uint16_t x) { return __builtin_bswap16(x); }
uint32_t (htonl)(uint32_t x) { return __builtin_bswap32(x); }
uint32_t (ntohl)(uint32_t x) { return __builtin_bswap32(x); }

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

/* ---- text to binary ---- */

/* Strict dotted quad: four decimal parts 0-255, no leading zeros. */
static int pton4(const char *s, unsigned char *out, const char **end)
{
	for (int i = 0; i < 4; i++) {
		if ((unsigned)*s - '0' >= 10)
			return 0;
		if (s[0] == '0' && (unsigned)s[1] - '0' < 10)
			return 0;
		int v = 0, n = 0;
		for (; (unsigned)*s - '0' < 10 && n < 3; n++)
			v = v * 10 + (*s++ - '0');
		if ((unsigned)*s - '0' < 10 || v > 255)
			return 0;
		out[i] = (unsigned char)v;
		if (i < 3 && *s++ != '.')
			return 0;
	}
	if (end)
		*end = s;
	return 1;
}

static int pton6(const char *s, unsigned char *out)
{
	uint16_t g[8];
	int n = 0, gap = -1;
	if (s[0] == ':') {
		if (s[1] != ':')
			return 0;
		s++;
	}
	while (*s) {
		if (*s == ':') {
			if (gap >= 0)
				return 0; /* "::" only once */
			gap = n;
			s++;
			if (!*s)
				break;
			continue;
		}
		if (n == 8)
			return 0;
		/* embedded IPv4 in the last 32 bits */
		const char *dot = s;
		while ((unsigned)*dot - '0' < 10)
			dot++;
		if (*dot == '.') {
			unsigned char v4[4];
			const char *e;
			if (n > 6 || !pton4(s, v4, &e) || *e)
				return 0;
			g[n++] = (uint16_t)(v4[0] << 8 | v4[1]);
			g[n++] = (uint16_t)(v4[2] << 8 | v4[3]);
			s = e;
			break;
		}
		unsigned v = 0;
		int d = 0;
		for (; d < 5; d++, s++) {
			unsigned c = (unsigned char)*s;
			if (c - '0' < 10) v = v * 16 + c - '0';
			else if ((c | 32) - 'a' < 6) v = v * 16 + (c | 32) - 'a' + 10;
			else break;
		}
		if (!d || d > 4)
			return 0;
		g[n++] = (uint16_t)v;
		if (*s == ':') {
			s++;
			if (!*s)
				return 0; /* trailing single ':' */
			if (*s == ':')
				continue;
		} else if (*s) {
			return 0;
		}
	}
	if (gap < 0 ? n != 8 : n == 8)
		return 0;
	int fill = 8 - n;
	for (int i = 0, j = 0; i < 8; i++) {
		uint16_t v = 0;
		if (gap >= 0 && i >= gap && i < gap + fill)
			v = 0;
		else
			v = g[j++];
		out[2 * i] = (unsigned char)(v >> 8);
		out[2 * i + 1] = (unsigned char)v;
	}
	return 1;
}

int inet_pton(int af, const char *__restrict s, void *__restrict dst)
{
	unsigned char buf[16];
	if (af == AF_INET) {
		const char *e;
		if (!pton4(s, buf, &e) || *e)
			return 0;
		memcpy(dst, buf, 4);
		return 1;
	}
	if (af == AF_INET6) {
		if (!pton6(s, buf))
			return 0;
		memcpy(dst, buf, 16);
		return 1;
	}
	errno = EAFNOSUPPORT;
	return -1;
}

/* Legacy form: 1-4 parts, each decimal, octal (0) or hex (0x). */
int inet_aton(const char *s, struct in_addr *dst)
{
	unsigned long part[4];
	int n = 0;
	for (;;) {
		if ((unsigned)*s - '0' >= 10)
			return 0;
		int base = 10;
		if (s[0] == '0') {
			if ((s[1] | 32) == 'x') {
				base = 16;
				s += 2;
			} else {
				base = 8;
			}
		}
		unsigned long v = 0;
		int any = 0;
		for (;; s++) {
			int d = (unsigned)*s - '0' < 10 ? *s - '0' : (unsigned)(*s | 32) - 'a' < 6 ? (*s | 32) - 'a' + 10 : 99;
			if (d >= base)
				break;
			v = v * (unsigned)base + (unsigned)d;
			if (v > 0xffffffffUL)
				return 0;
			any = 1;
		}
		if (!any && base != 8)
			return 0;
		part[n++] = v;
		if (*s != '.' || n == 4)
			break;
		s++;
	}
	if (*s && *s != ' ')
		return 0;
	unsigned long a;
	switch (n) {
	case 1: a = part[0]; break;
	case 2: if (part[0] > 255 || part[1] > 0xffffff) return 0; a = part[0] << 24 | part[1]; break;
	case 3: if (part[0] > 255 || part[1] > 255 || part[2] > 0xffff) return 0; a = part[0] << 24 | part[1] << 16 | part[2]; break;
	default:
		for (int i = 0; i < 4; i++)
			if (part[i] > 255)
				return 0;
		a = part[0] << 24 | part[1] << 16 | part[2] << 8 | part[3];
	}
	if (dst)
		dst->s_addr = htonl((uint32_t)a);
	return 1;
}

in_addr_t inet_addr(const char *s)
{
	struct in_addr a;
	return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

/* ---- binary to text ---- */

const char *inet_ntop(int af, const void *__restrict src, char *__restrict dst, socklen_t len)
{
	const unsigned char *a = src;
	char buf[INET6_ADDRSTRLEN];
	if (af == AF_INET) {
		snprintf(buf, sizeof buf, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
	} else if (af == AF_INET6) {
		uint16_t g[8];
		for (int i = 0; i < 8; i++)
			g[i] = (uint16_t)(a[2 * i] << 8 | a[2 * i + 1]);
		char *p = buf;
		/* IPv4-mapped and IPv4-compatible forms */
		if (!g[0] && !g[1] && !g[2] && !g[3] && !g[4] && (g[5] == 0xffff || (!g[5] && g[6]))) {
			snprintf(buf, sizeof buf, "::%s%u.%u.%u.%u", g[5] ? "ffff:" : "", a[12], a[13], a[14], a[15]);
		} else {
			/* RFC 5952: compress the longest run of two or more zero
			 * groups (the first if tied) */
			int best = -1, blen = 1;
			for (int i = 0; i < 8;) {
				if (g[i]) {
					i++;
					continue;
				}
				int j = i;
				while (j < 8 && !g[j])
					j++;
				if (j - i > blen) {
					best = i;
					blen = j - i;
				}
				i = j;
			}
			for (int i = 0; i < 8; i++) {
				if (i == best) {
					*p++ = ':';
					if (i == 0)
						*p++ = ':';
					i += blen - 1;
					continue;
				}
				p += sprintf(p, "%x", g[i]);
				if (i < 7)
					*p++ = ':';
			}
			*p = 0;
		}
	} else {
		errno = EAFNOSUPPORT;
		return 0;
	}
	size_t l = strlen(buf);
	if (l >= len) {
		errno = ENOSPC;
		return 0;
	}
	memcpy(dst, buf, l + 1);
	return dst;
}

char *inet_ntoa(struct in_addr in)
{
	static char buf[INET_ADDRSTRLEN];
	return (char *)inet_ntop(AF_INET, &in, buf, sizeof buf);
}
