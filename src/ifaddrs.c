/* getifaddrs: link-layer entries (AF_PACKET) for every interface, then
 * IPv4 addresses from SIOCGIFCONF and IPv6 ones from /proc/net/if_inet6,
 * in that order as glibc returns them. */
#include <ifaddrs.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

union sa {
	struct sockaddr sa;
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
	struct sockaddr_ll ll;
};

struct node {
	struct ifaddrs ifa;
	union sa addr, mask, brd;
	char name[IFNAMSIZ];
};

void freeifaddrs(struct ifaddrs *p)
{
	while (p) {
		struct ifaddrs *n = p->ifa_next;
		free(p);
		p = n;
	}
}

static struct node *add(struct ifaddrs ***tail, const char *name, unsigned flags)
{
	struct node *n = calloc(1, sizeof *n);
	if (!n)
		return 0;
	strncpy(n->name, name, IFNAMSIZ - 1);
	n->ifa.ifa_name = n->name;
	n->ifa.ifa_flags = flags;
	**tail = &n->ifa;
	*tail = &n->ifa.ifa_next;
	return n;
}

static unsigned if_flags(int fd, const char *name)
{
	struct ifreq r;
	memset(&r, 0, sizeof r);
	strncpy(r.ifr_name, name, IFNAMSIZ - 1);
	return ioctl(fd, SIOCGIFFLAGS, &r) < 0 ? 0 : (unsigned short)r.ifr_flags;
}

static int get_sa(int fd, unsigned long req, const char *name, struct sockaddr_in *out)
{
	struct ifreq r;
	memset(&r, 0, sizeof r);
	strncpy(r.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(fd, req, &r) < 0 || r.ifr_addr.sa_family != AF_INET)
		return 0;
	memcpy(out, &r.ifr_addr, sizeof *out);
	return 1;
}

int getifaddrs(struct ifaddrs **res)
{
	struct ifaddrs *head = 0, **tail = &head;
	int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;

	struct if_nameindex *ni = if_nameindex();
	for (struct if_nameindex *p = ni; p && p->if_index; p++) {
		unsigned fl = if_flags(fd, p->if_name);
		struct node *n = add(&tail, p->if_name, fl);
		if (!n)
			goto nomem;
		struct ifreq r;
		memset(&r, 0, sizeof r);
		strncpy(r.ifr_name, p->if_name, IFNAMSIZ - 1);
		n->addr.ll.sll_family = AF_PACKET;
		n->addr.ll.sll_ifindex = (int)p->if_index;
		if (ioctl(fd, SIOCGIFHWADDR, &r) == 0) {
			n->addr.ll.sll_hatype = (unsigned short)r.ifr_hwaddr.sa_family;
			n->addr.ll.sll_halen = 6;
			memcpy(n->addr.ll.sll_addr, r.ifr_hwaddr.sa_data, 6);
		}
		n->ifa.ifa_addr = &n->addr.sa;
		if (fl & IFF_BROADCAST) {
			n->brd.ll = n->addr.ll;
			memset(n->brd.ll.sll_addr, 0xff, 6);
			n->ifa.ifa_broadaddr = &n->brd.sa;
		}
	}
	if_freenameindex(ni);

	/* IPv4 */
	struct ifconf ic;
	char *buf = 0;
	for (int size = 4096; size <= 1 << 20; size *= 2) {
		char *nb = realloc(buf, (size_t)size);
		if (!nb)
			goto nomem_buf;
		buf = nb;
		ic.ifc_len = size;
		ic.ifc_buf = buf;
		if (ioctl(fd, SIOCGIFCONF, &ic) < 0) {
			ic.ifc_len = 0;
			break;
		}
		if (ic.ifc_len < size - (int)sizeof(struct ifreq))
			break;
	}
	for (int off = 0; off + (int)sizeof(struct ifreq) <= ic.ifc_len; off += (int)sizeof(struct ifreq)) {
		struct ifreq *r = (struct ifreq *)(buf + off);
		if (r->ifr_addr.sa_family != AF_INET)
			continue;
		char name[IFNAMSIZ];
		memcpy(name, r->ifr_name, IFNAMSIZ);
		name[IFNAMSIZ - 1] = 0;
		unsigned fl = if_flags(fd, name);
		struct node *n = add(&tail, name, fl);
		if (!n)
			goto nomem_buf;
		memcpy(&n->addr.v4, &r->ifr_addr, sizeof n->addr.v4);
		n->ifa.ifa_addr = &n->addr.sa;
		if (get_sa(fd, SIOCGIFNETMASK, name, &n->mask.v4))
			n->ifa.ifa_netmask = &n->mask.sa;
		if ((fl & IFF_BROADCAST) && get_sa(fd, SIOCGIFBRDADDR, name, &n->brd.v4))
			n->ifa.ifa_broadaddr = &n->brd.sa;
		else if ((fl & IFF_POINTOPOINT) && get_sa(fd, SIOCGIFDSTADDR, name, &n->brd.v4))
			n->ifa.ifa_dstaddr = &n->brd.sa;
	}
	free(buf);

	/* IPv6 */
	FILE *f = fopen("/proc/net/if_inet6", "re");
	char line[256];
	while (f && fgets(line, sizeof line, f)) {
		char hex[33], name[IFNAMSIZ + 1];
		unsigned idx, plen, scope, flags;
		if (sscanf(line, "%32s %x %x %x %x %16s", hex, &idx, &plen, &scope, &flags, name) != 6 || plen > 128)
			continue;
		struct node *n = add(&tail, name, if_flags(fd, name));
		if (!n) {
			fclose(f);
			goto nomem;
		}
		n->addr.v6.sin6_family = n->mask.v6.sin6_family = AF_INET6;
		for (int i = 0; i < 16; i++) {
			unsigned b;
			sscanf(hex + 2 * i, "%2x", &b);
			n->addr.v6.sin6_addr.s6_addr[i] = (unsigned char)b;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&n->addr.v6.sin6_addr) || IN6_IS_ADDR_MC_LINKLOCAL(&n->addr.v6.sin6_addr))
			n->addr.v6.sin6_scope_id = idx;
		for (unsigned i = 0; i < plen; i++)
			n->mask.v6.sin6_addr.s6_addr[i / 8] |= (unsigned char)(0x80 >> (i % 8));
		n->ifa.ifa_addr = &n->addr.sa;
		n->ifa.ifa_netmask = &n->mask.sa;
	}
	if (f)
		fclose(f);
	close(fd);
	*res = head;
	return 0;

nomem_buf:
	free(buf);
nomem:
	close(fd);
	freeifaddrs(head);
	errno = ENOMEM;
	return -1;
}
