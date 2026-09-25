/* DNS stub resolver and /etc/hosts lookups used by getaddrinfo and
 * friends.
 *
 * Queries go over UDP to every configured name server at once (A and
 * AAAA together), with a random ID and a kernel-chosen random source
 * port; truncated answers are retried over TCP. A response is accepted
 * only from a configured server and port 53, with a matching ID, the QR
 * bit and exactly our question. All parsing is bounds-checked, name
 * compression may only point backwards, and names are rejected unless
 * every label consists of letters, digits, '-' and '_'. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "internal.h"
#include "resolv.h"

/* ---- configuration ---- */

struct resconf {
	struct sockaddr_in6 ns[MAXNS]; /* IPv4 servers as v4-mapped */
	int nns;
	int ndots, timeout, attempts;
	char search[256];
};

static void read_conf(struct resconf *c)
{
	memset(c, 0, sizeof *c);
	c->ndots = 1;
	c->timeout = 5;
	c->attempts = 2;
	FILE *f = fopen("/etc/resolv.conf", "re");
	char line[512];
	while (f && fgets(line, sizeof line, f)) {
		char *p = line, *e;
		if (!strchr(line, '\n') && !feof(f)) { /* overlong line: skip it */
			int ch;
			while ((ch = getc(f)) != EOF && ch != '\n')
				;
			continue;
		}
		p[strcspn(p, "#;\n")] = 0;
		if (!strncmp(p, "nameserver", 10) && (p[10] == ' ' || p[10] == '\t')) {
			p += 11;
			p += strspn(p, " \t");
			p[strcspn(p, " \t")] = 0;
			if (c->nns == MAXNS)
				continue;
			struct sockaddr_in6 *s = &c->ns[c->nns];
			struct in_addr a4;
			char *pct = strchr(p, '%');
			if (pct)
				*pct = 0;
			if (inet_pton(AF_INET, p, &a4) == 1) {
				memset(s, 0, sizeof *s);
				s->sin6_addr.s6_addr[10] = s->sin6_addr.s6_addr[11] = 0xff;
				memcpy(&s->sin6_addr.s6_addr[12], &a4, 4);
			} else if (inet_pton(AF_INET6, p, &s->sin6_addr) == 1) {
				if (pct) {
					unsigned long id = strtoul(pct + 1, &e, 10);
					s->sin6_scope_id = *e ? if_nametoindex(pct + 1) : (unsigned)id;
				}
			} else {
				continue;
			}
			s->sin6_family = AF_INET6;
			s->sin6_port = htons(53);
			c->nns++;
		} else if ((!strncmp(p, "search", 6) && (p[6] == ' ' || p[6] == '\t')) ||
		           (!strncmp(p, "domain", 6) && (p[6] == ' ' || p[6] == '\t'))) {
			p += 7;
			p += strspn(p, " \t");
			size_t n = strlen(p);
			while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r'))
				p[--n] = 0;
			if (n < sizeof c->search)
				memcpy(c->search, p, n + 1);
		} else if (!strncmp(p, "options", 7)) {
			for (char *o = strtok_r(p + 7, " \t\r", &e); o; o = strtok_r(0, " \t\r", &e)) {
				if (!strncmp(o, "ndots:", 6))
					c->ndots = atoi(o + 6);
				else if (!strncmp(o, "timeout:", 8))
					c->timeout = atoi(o + 8);
				else if (!strncmp(o, "attempts:", 9))
					c->attempts = atoi(o + 9);
			}
		}
	}
	if (f)
		fclose(f);
	if (c->ndots < 0) c->ndots = 0;
	if (c->ndots > 15) c->ndots = 15;
	if (c->timeout < 1) c->timeout = 1;
	if (c->timeout > 30) c->timeout = 30;
	if (c->attempts < 1) c->attempts = 1;
	if (c->attempts > 5) c->attempts = 5;
	if (!c->nns) {
		struct sockaddr_in6 *s = &c->ns[0];
		memset(s, 0, sizeof *s);
		s->sin6_family = AF_INET6;
		s->sin6_port = htons(53);
		s->sin6_addr.s6_addr[10] = s->sin6_addr.s6_addr[11] = 0xff;
		s->sin6_addr.s6_addr[12] = 127;
		s->sin6_addr.s6_addr[15] = 1;
		c->nns = 1;
	}
}

/* ---- names ---- */

static int label_char_ok(unsigned char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
}

hidden int __valid_hostname(const char *s)
{
	size_t n = strlen(s);
	if (!n || n > 253)
		return 0;
	size_t lab = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '.') {
			if (!lab)
				return 0;
			lab = 0;
		} else if (!label_char_ok((unsigned char)s[i]) || ++lab > 63) {
			return 0;
		}
	}
	return 1;
}

/* Read a possibly compressed name at off. Output is dotted, without a
 * trailing dot, at most 253 characters. Returns the offset just past the
 * name in place, or -1. */
static int get_name(const unsigned char *m, int len, int off, char *out)
{
	int end = -1, jumps = 0, o = 0;
	for (;;) {
		if (off >= len)
			return -1;
		unsigned c = m[off];
		if ((c & 0xc0) == 0xc0) {
			if (off + 1 >= len || ++jumps > 64)
				return -1;
			int to = (int)((c & 0x3f) << 8 | m[off + 1]);
			if (end < 0)
				end = off + 2;
			if (to >= off) /* only backwards: no loops */
				return -1;
			off = to;
			continue;
		}
		if (c & 0xc0)
			return -1;
		if (!c) {
			out[o] = 0;
			return end < 0 ? off + 1 : end;
		}
		if (off + 1 + (int)c > len || o + (int)c + 1 > 253)
			return -1;
		if (o)
			out[o++] = '.';
		for (unsigned i = 0; i < c; i++) {
			unsigned char ch = m[off + 1 + i];
			if (!label_char_ok(ch))
				return -1;
			out[o++] = (char)ch;
		}
		off += 1 + (int)c;
	}
}

/* Build a query; returns its length or -1 for an unusable name. */
static int mkquery(const char *name, int type, unsigned char *q, unsigned id)
{
	int o = 12;
	memset(q, 0, 12);
	q[0] = (unsigned char)(id >> 8);
	q[1] = (unsigned char)id;
	q[2] = 0x01; /* RD */
	q[5] = 1;    /* QDCOUNT */
	const char *p = name;
	if (!*p)
		return -1;
	while (*p) {
		const char *d = strchr(p, '.');
		size_t n = d ? (size_t)(d - p) : strlen(p);
		if (!n || n > 63 || o + 1 + (int)n > 12 + 254)
			return -1;
		q[o++] = (unsigned char)n;
		memcpy(q + o, p, n);
		o += (int)n;
		p += n;
		if (*p == '.')
			p++;
	}
	q[o++] = 0;
	q[o++] = (unsigned char)(type >> 8);
	q[o++] = (unsigned char)type;
	q[o++] = 0;
	q[o++] = 1; /* IN */
	return o;
}

/* ---- transport ---- */

static int same_server(const struct sockaddr_in6 *a, const struct sockaddr_in6 *b)
{
	return a->sin6_port == b->sin6_port && !memcmp(&a->sin6_addr, &b->sin6_addr, 16);
}

static long elapsed_ms(const struct timespec *t0)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1000 + (t.tv_nsec - t0->tv_nsec) / 1000000;
}

/* TCP retry for a truncated answer: returns the answer length or -1 */
static int tcp_query(const struct sockaddr_in6 *ns, const unsigned char *q, int ql, unsigned char *a, int timeout_ms)
{
	int v4 = IN6_IS_ADDR_V4MAPPED(&ns->sin6_addr);
	int fd = socket(v4 ? AF_INET : AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -1;
	struct sockaddr_in s4 = { .sin_family = AF_INET, .sin_port = ns->sin6_port };
	if (v4)
		memcpy(&s4.sin_addr, &ns->sin6_addr.s6_addr[12], 4);
	const struct sockaddr *sa = v4 ? (const struct sockaddr *)&s4 : (const struct sockaddr *)ns;
	socklen_t sl = v4 ? sizeof s4 : sizeof *ns;
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	unsigned char out[2 + 300];
	out[0] = (unsigned char)(ql >> 8);
	out[1] = (unsigned char)ql;
	memcpy(out + 2, q, (size_t)ql);
	int sent = 0, got = 0, want = -1, r = -1;
	unsigned char lenbuf[2];
	if (connect(fd, sa, sl) < 0 && errno != EINPROGRESS)
		goto done;
	for (;;) {
		long left = timeout_ms - elapsed_ms(&t0);
		if (left <= 0)
			goto done;
		struct pollfd p = { fd, sent < ql + 2 ? POLLOUT : POLLIN, 0 };
		if (poll(&p, 1, (int)left) <= 0)
			continue;
		if (sent < ql + 2) {
			ssize_t n = send(fd, out + sent, (size_t)(ql + 2 - sent), MSG_NOSIGNAL);
			if (n < 0 && errno != EAGAIN)
				goto done;
			if (n > 0)
				sent += (int)n;
			continue;
		}
		if (want < 0) {
			ssize_t n = recv(fd, lenbuf + got, (size_t)(2 - got), 0);
			if (n <= 0 && !(n < 0 && errno == EAGAIN))
				goto done;
			if (n > 0 && (got += (int)n) == 2) {
				want = lenbuf[0] << 8 | lenbuf[1];
				got = 0;
				if (want < 12)
					goto done;
			}
			continue;
		}
		ssize_t n = recv(fd, a + got, (size_t)(want - got), 0);
		if (n <= 0 && !(n < 0 && errno == EAGAIN))
			goto done;
		if (n > 0 && (got += (int)n) == want) {
			r = want;
			goto done;
		}
	}
done:
	close(fd);
	return r;
}

/* A response to query q: same ID, QR set, one question equal to ours
 * (the name compared without regard to case). */
static int question_matches(const unsigned char *a, int al, const unsigned char *q, int ql)
{
	if (al < ql || a[0] != q[0] || a[1] != q[1] || !(a[2] & 0x80) || a[4] || a[5] != 1)
		return 0;
	for (int j = 12; j < ql; j++) {
		unsigned char x = a[j], y = q[j];
		if (x >= 'A' && x <= 'Z')
			x += 32;
		if (y >= 'A' && y <= 'Z')
			y += 32;
		if (x != y)
			return 0;
	}
	return 1;
}

/* Send nq queries; fill answers (buffers of ANSZ bytes) and lengths
 * (0 when there was no usable answer). Returns 0, or -1 if no socket. */
static int dns_exchange(const struct resconf *c, unsigned char *const *qs, const int *ql,
                        unsigned char *const *as, int *al, int nq)
{
	int fam = AF_INET6;
	int fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd >= 0) {
		int off = 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
	} else {
		fam = AF_INET;
		fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
		if (fd < 0)
			return -1;
	}
	int servfail[MAXNS * 2] = { 0 };
	for (int i = 0; i < nq; i++)
		al[i] = 0;
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	long total = (long)c->timeout * c->attempts * 1000, next_send = 0;
	for (;;) {
		long now = elapsed_ms(&t0);
		int pending = 0;
		for (int i = 0; i < nq; i++)
			pending += !al[i];
		if (!pending || now >= total)
			break;
		if (now >= next_send) {
			for (int i = 0; i < nq; i++) {
				if (al[i])
					continue;
				for (int k = 0; k < c->nns; k++) {
					const struct sockaddr_in6 *ns = &c->ns[k];
					if (fam == AF_INET6) {
						sendto(fd, qs[i], (size_t)ql[i], MSG_NOSIGNAL, (const struct sockaddr *)ns, sizeof *ns);
					} else if (IN6_IS_ADDR_V4MAPPED(&ns->sin6_addr)) {
						struct sockaddr_in s4 = { .sin_family = AF_INET, .sin_port = ns->sin6_port };
						memcpy(&s4.sin_addr, &ns->sin6_addr.s6_addr[12], 4);
						sendto(fd, qs[i], (size_t)ql[i], MSG_NOSIGNAL, (struct sockaddr *)&s4, sizeof s4);
					}
				}
			}
			next_send = now + (long)c->timeout * 1000;
		}
		struct pollfd p = { fd, POLLIN, 0 };
		long wait = (next_send < total ? next_send : total) - now;
		if (poll(&p, 1, (int)(wait > 0 ? wait : 1)) <= 0)
			continue;
		for (;;) {
			unsigned char buf[ANSZ_UDP];
			struct sockaddr_in6 from;
			struct sockaddr_in *f4 = (struct sockaddr_in *)&from;
			socklen_t fl = sizeof from;
			ssize_t n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
			if (n < 0)
				break;
			if (fam == AF_INET) { /* normalise to v4-mapped */
				struct sockaddr_in s4 = *f4;
				memset(&from, 0, sizeof from);
				from.sin6_family = AF_INET6;
				from.sin6_port = s4.sin_port;
				from.sin6_addr.s6_addr[10] = from.sin6_addr.s6_addr[11] = 0xff;
				memcpy(&from.sin6_addr.s6_addr[12], &s4.sin_addr, 4);
			}
			int k;
			for (k = 0; k < c->nns && !same_server(&from, &c->ns[k]); k++)
				;
			if (k == c->nns || n < 12 || !(buf[2] & 0x80))
				continue;
			int i;
			for (i = 0; i < nq; i++)
				if (!al[i] && buf[0] == qs[i][0] && buf[1] == qs[i][1])
					break;
			if (i == nq)
				continue;
			if (!question_matches(buf, (int)n, qs[i], ql[i]))
				continue;
			int rcode = buf[3] & 15;
			if (rcode != 0 && rcode != 3) {
				/* SERVFAIL, REFUSED...: wait for another server unless
				 * this was the last one */
				if (++servfail[i] < c->nns)
					continue;
			}
			if (buf[2] & 0x02) { /* truncated: ask again over TCP */
				int r = tcp_query(&c->ns[k], qs[i], ql[i], as[i], c->timeout * 1000);
				if (r > 0 && question_matches(as[i], r, qs[i], ql[i]))
					al[i] = r;
				continue;
			}
			memcpy(as[i], buf, (size_t)n);
			al[i] = (int)n;
		}
	}
	close(fd);
	return 0;
}

/* ---- answer parsing ---- */

static int eq_name(const char *a, const char *b) { return !strcasecmp(a, b); }

/* Parse an answer: collect records of type qtype for qname (following
 * CNAMEs). Returns 0 with *n records (maybe none), DNS_NXDOMAIN or
 * DNS_FAIL. For A/AAAA the records go to out; for PTR the name goes to
 * ptr (NAME_BUF bytes). The final CNAME target goes to canon. */
static int parse_answer(const unsigned char *m, int len, const char *qname, int qtype,
                        struct dns_addr *out, int *n, int max, char *ptr, char *canon)
{
	if (len < 12)
		return DNS_FAIL;
	int rcode = m[3] & 15;
	if (rcode == 3)
		return DNS_NXDOMAIN;
	if (rcode)
		return DNS_FAIL;
	int qd = m[4] << 8 | m[5], an = m[6] << 8 | m[7];
	int off = 12;
	char name[256];
	for (int i = 0; i < qd; i++) {
		if ((off = get_name(m, len, off, name)) < 0 || off + 4 > len)
			return DNS_FAIL;
		off += 4;
	}
	char target[256];
	strcpy(target, qname);
	/* record offsets, so the CNAME chain can be followed in any order */
	int roff[64], nr = 0;
	for (int i = 0; i < an && nr < 64; i++) {
		roff[nr++] = off;
		if ((off = get_name(m, len, off, name)) < 0 || off + 10 > len)
			return DNS_FAIL;
		int rdlen = m[off + 8] << 8 | m[off + 9];
		off += 10 + rdlen;
		if (off > len)
			return DNS_FAIL;
	}
	for (int hops = 0; hops < 16; hops++) {
		int moved = 0;
		for (int i = 0; i < nr; i++) {
			int o = get_name(m, len, roff[i], name);
			if (o < 0)
				continue;
			int type = m[o] << 8 | m[o + 1], cls = m[o + 2] << 8 | m[o + 3];
			if (type == 5 && cls == 1 && eq_name(name, target)) {
				char cn[256];
				if (get_name(m, len, o + 10, cn) < 0)
					return DNS_FAIL;
				strcpy(target, cn);
				moved = 1;
				break;
			}
		}
		if (!moved)
			break;
	}
	if (canon)
		strcpy(canon, target);
	*n = 0;
	for (int i = 0; i < nr; i++) {
		int o = get_name(m, len, roff[i], name);
		if (o < 0 || !eq_name(name, target))
			continue;
		int type = m[o] << 8 | m[o + 1], cls = m[o + 2] << 8 | m[o + 3];
		int rdlen = m[o + 8] << 8 | m[o + 9];
		const unsigned char *rd = m + o + 10;
		if (cls != 1 || type != qtype)
			continue;
		if (qtype == T_A && rdlen == 4 && *n < max) {
			out[*n].family = AF_INET;
			memcpy(out[*n].addr, rd, 4);
			out[*n].scope = 0;
			++*n;
		} else if (qtype == T_AAAA && rdlen == 16 && *n < max) {
			out[*n].family = AF_INET6;
			memcpy(out[*n].addr, rd, 16);
			out[*n].scope = 0;
			++*n;
		} else if (qtype == T_PTR && ptr && get_name(m, len, o + 10, ptr) > 0 && *ptr) {
			*n = 1;
			return 0;
		}
	}
	return 0;
}

/* ---- /etc/hosts ---- */

static int parse_addr(const char *s, struct dns_addr *a)
{
	char buf[64];
	size_t n = strlen(s);
	if (n >= sizeof buf)
		return 0;
	memcpy(buf, s, n + 1);
	char *pct = strchr(buf, '%');
	if (pct)
		*pct = 0;
	a->scope = 0;
	if (inet_pton(AF_INET, buf, a->addr) == 1) {
		a->family = AF_INET;
		return 1;
	}
	if (inet_pton(AF_INET6, buf, a->addr) == 1) {
		a->family = AF_INET6;
		if (pct) {
			char *e;
			unsigned long id = strtoul(pct + 1, &e, 10);
			a->scope = *e ? if_nametoindex(pct + 1) : (unsigned)id;
		}
		return 1;
	}
	return 0;
}

static int hosts_forward(const char *name, int family, struct dns_addr *out, int *n, int max, char *canon)
{
	FILE *f = fopen("/etc/hosts", "re");
	if (!f)
		return 0;
	char line[512];
	int found = 0;
	*n = 0;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "#\n")] = 0;
		char *save, *tok = strtok_r(line, " \t\r", &save);
		struct dns_addr a;
		if (!tok || !parse_addr(tok, &a))
			continue;
		char *first = strtok_r(0, " \t\r", &save);
		int hit = 0;
		for (char *h = first; h; h = strtok_r(0, " \t\r", &save))
			if (eq_name(h, name))
				hit = 1;
		if (!hit)
			continue;
		if (!found && canon && strlen(first) < NAME_BUF)
			strcpy(canon, first);
		found = 1;
		if ((family == AF_UNSPEC || family == a.family) && *n < max)
			out[(*n)++] = a;
	}
	fclose(f);
	return found;
}

static int hosts_reverse(const struct dns_addr *a, char *name)
{
	FILE *f = fopen("/etc/hosts", "re");
	if (!f)
		return 0;
	char line[512];
	int found = 0;
	while (!found && fgets(line, sizeof line, f)) {
		line[strcspn(line, "#\n")] = 0;
		char *save, *tok = strtok_r(line, " \t\r", &save);
		struct dns_addr b;
		if (!tok || !parse_addr(tok, &b) || b.family != a->family)
			continue;
		if (memcmp(a->addr, b.addr, a->family == AF_INET ? 4 : 16))
			continue;
		char *h = strtok_r(0, " \t\r", &save);
		if (h && strlen(h) < NAME_BUF) {
			strcpy(name, h);
			found = 1;
		}
	}
	fclose(f);
	return found;
}

/* ---- lookups ---- */

/* Query one fully qualified name for the wanted families. */
static int query_name(const struct resconf *c, const char *name, int family, struct dns_addr *out, int *n,
                      int max, char *canon)
{
	unsigned char qbuf[2][300];
	unsigned char *qs[2] = { qbuf[0], qbuf[1] };
	int ql[2], types[2], nq = 0;
	if (family != AF_INET6)
		types[nq++] = T_A;
	if (family != AF_INET)
		types[nq++] = T_AAAA;
	unsigned short ids[2];
	arc4random_buf(ids, sizeof ids);
	if (nq == 2 && ids[0] == ids[1])
		ids[1] ^= 1;
	for (int i = 0; i < nq; i++)
		if ((ql[i] = mkquery(name, types[i], qs[i], ids[i])) < 0)
			return DNS_NXDOMAIN;
	unsigned char *abuf = malloc((size_t)ANSZ * 2);
	if (!abuf)
		return DNS_FAIL;
	unsigned char *as[2] = { abuf, abuf + ANSZ };
	int al[2];
	if (dns_exchange(c, qs, ql, as, al, nq) < 0) {
		free(abuf);
		return DNS_FAIL;
	}
	int r = DNS_FAIL, nx = 0;
	*n = 0;
	char cn[256] = "";
	for (int i = 0; i < nq; i++) {
		if (!al[i])
			continue;
		int k = 0;
		int s = parse_answer(as[i], al[i], name, types[i], out + *n, &k, max - *n, 0, cn);
		if (s == DNS_NXDOMAIN) {
			nx++;
		} else if (s == 0) {
			*n += k;
			r = 0;
			if (canon && *cn && !*canon)
				strcpy(canon, cn);
		}
	}
	free(abuf);
	if (*n)
		return 0;
	if (nx && r)
		return DNS_NXDOMAIN;
	return r == 0 ? DNS_NODATA : DNS_FAIL;
}

hidden int __lookup_name(const char *name, int family, struct dns_addr *out, int *n, char *canon)
{
	*n = 0;
	canon[0] = 0;
	size_t len = strlen(name);
	if (!len || len > 254)
		return EAI_NONAME;
	if (hosts_forward(name, family, out, n, MAXADDRS, canon))
		return *n ? 0 : EAI_NODATA;

	struct resconf c;
	read_conf(&c);
	char buf[256];
	int absolute = name[len - 1] == '.';
	if (absolute)
		len--;
	if (!len || len > 253)
		return EAI_NONAME;
	memcpy(buf, name, len);
	buf[len] = 0;
	if (!__valid_hostname(buf))
		return EAI_NONAME;
	int dots = 0;
	for (size_t i = 0; i < len; i++)
		dots += buf[i] == '.';

	int again = 0, nodata = 0;
	/* candidates: the name itself first when it has enough dots, and the
	 * search domains */
	for (int pass = 0; pass < 2; pass++) {
		int as_is = (pass == 0) == (dots >= c.ndots);
		if (as_is || absolute) {
			if (absolute && pass)
				break;
			int s = query_name(&c, buf, family, out, n, MAXADDRS, canon);
			if (s == 0) {
				if (!*canon)
					strcpy(canon, buf);
				return 0;
			}
			again |= s == DNS_FAIL;
			nodata |= s == DNS_NODATA;
			continue;
		}
		char search[256];
		strcpy(search, c.search);
		char *save;
		for (char *d = strtok_r(search, " \t", &save); d; d = strtok_r(0, " \t", &save)) {
			char full[256];
			size_t dl = strlen(d);
			while (dl && d[dl - 1] == '.')
				dl--;
			if (!dl || len + 1 + dl > 253)
				continue;
			memcpy(full, buf, len);
			full[len] = '.';
			memcpy(full + len + 1, d, dl);
			full[len + 1 + dl] = 0;
			int s = query_name(&c, full, family, out, n, MAXADDRS, canon);
			if (s == 0) {
				if (!*canon)
					strcpy(canon, full);
				return 0;
			}
			again |= s == DNS_FAIL;
			nodata |= s == DNS_NODATA;
		}
	}
	if (again)
		return EAI_AGAIN;
	return nodata ? EAI_NODATA : EAI_NONAME;
}

hidden int __lookup_addr(const struct dns_addr *a, char *name)
{
	if (hosts_reverse(a, name))
		return 0;
	char q[80];
	int o = 0;
	if (a->family == AF_INET) {
		o = snprintf(q, sizeof q, "%d.%d.%d.%d.in-addr.arpa", a->addr[3], a->addr[2], a->addr[1], a->addr[0]);
	} else {
		for (int i = 15; i >= 0; i--)
			o += snprintf(q + o, sizeof q - (size_t)o, "%x.%x.", a->addr[i] & 15, a->addr[i] >> 4);
		strcpy(q + o, "ip6.arpa");
	}
	struct resconf c;
	read_conf(&c);
	unsigned char qb[300];
	unsigned char *qs[1] = { qb };
	unsigned short id;
	arc4random_buf(&id, sizeof id);
	int ql[1] = { mkquery(q, T_PTR, qb, id) };
	unsigned char *ab = malloc(ANSZ);
	if (!ab)
		return EAI_MEMORY;
	unsigned char *as[1] = { ab };
	int al[1];
	int r = EAI_AGAIN;
	if (ql[0] > 0 && dns_exchange(&c, qs, ql, as, al, 1) == 0 && al[0]) {
		int n = 0;
		int s = parse_answer(ab, al[0], q, T_PTR, 0, &n, 0, name, 0);
		r = s == DNS_FAIL ? EAI_AGAIN : n ? 0 : EAI_NONAME;
	}
	free(ab);
	return r;
}
