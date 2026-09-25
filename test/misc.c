/* Directory streams, getopt, fnmatch, libgen, passwd/group, sockets and
 * address conversion, I/O multiplexing, wide character classes, and the
 * assorted system interfaces. */
#include "harness.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <grp.h>
#include <libgen.h>
#include <locale.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void dirs(void)
{
	char d[] = "/tmp/citadel-dir-XXXXXX";
	CHECK(mkdtemp(d) != 0);
	char p[128];
	const char *names[] = { "a", "bb", "ccc" };
	for (int i = 0; i < 3; i++) {
		snprintf(p, sizeof p, "%s/%s", d, names[i]);
		close(open(p, O_CREAT | O_WRONLY, 0600));
	}
	snprintf(p, sizeof p, "%s/sub", d);
	mkdir(p, 0700);
	DIR *dir = opendir(d);
	CHECK(dir != 0 && dirfd(dir) >= 0);
	char *seen[8];
	int n = 0, types = 0;
	struct dirent *de;
	while ((de = readdir(dir)) && n < 8) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		seen[n++] = strdup(de->d_name);
		if (!strcmp(de->d_name, "sub"))
			types += de->d_type == DT_DIR;
		else
			types += de->d_type == DT_REG;
	}
	CHECK(n == 4 && types == 4);
	qsort(seen, (size_t)n, sizeof *seen, cmpstr);
	CHECK(n == 4 && !strcmp(seen[0], "a") && !strcmp(seen[3], "sub"));
	for (int i = 0; i < n; i++)
		free(seen[i]);
	rewinddir(dir);
	int again = 0;
	while (readdir(dir))
		again++;
	CHECK(again == 6);
	CHECK(closedir(dir) == 0);
	struct dirent **list;
	n = scandir(d, &list, 0, alphasort);
	CHECK(n == 6 && !strcmp(list[0]->d_name, ".") && !strcmp(list[5]->d_name, "sub"));
	for (int i = 0; i < n; i++)
		free(list[i]);
	free(list);
	errno = 0;
	CHECK(opendir("/nonexistent") == 0 && errno == ENOENT);
	snprintf(p, sizeof p, "%s/a", d);
	CHECK(opendir(p) == 0 && errno == ENOTDIR);
	for (int i = 0; i < 3; i++) {
		snprintf(p, sizeof p, "%s/%s", d, names[i]);
		unlink(p);
	}
	snprintf(p, sizeof p, "%s/sub", d);
	rmdir(p);
	CHECK(rmdir(d) == 0);
}

static void options(void)
{
	char *av[] = { "prog", "-ab", "val", "-c", "--", "-x", 0 };
	int ac = 6, c, got = 0;
	optind = 1;
	while ((c = getopt(ac, av, "ab:c")) != -1)
		got = got * 10 + (c == 'a' ? 1 : c == 'b' ? 2 : c == 'c' ? 3 : 9);
	CHECK(got == 123 && optind == 5 && !strcmp(av[optind], "-x"));
	opterr = 0;
	char *av2[] = { "prog", "-z", "-b", 0 };
	optind = 1;
	CHECK(getopt(3, av2, ":b:") == '?' && optopt == 'z');
	CHECK(getopt(3, av2, ":b:") == ':' && optopt == 'b');

	static int flag;
	struct option lo[] = { { "verbose", no_argument, 0, 'v' }, { "out", required_argument, 0, 'o' },
	                       { "flag", no_argument, &flag, 7 }, { 0, 0, 0, 0 } };
	char *av3[] = { "prog", "file1", "--verb", "--out=x", "file2", "--flag", "-o", "y", 0 };
	optind = 0;
	int idx = -1, v = 0;
	char outs[8] = "";
	while ((c = getopt_long(8, av3, "vo:", lo, &idx)) != -1) {
		if (c == 'v')
			v++;
		else if (c == 'o')
			strcat(outs, optarg);
	}
	/* operands are permuted to the end */
	CHECK(v == 1 && !strcmp(outs, "xy") && flag == 7 && optind == 6);
	CHECK(!strcmp(av3[6], "file1") && !strcmp(av3[7], "file2"));
	char *av4[] = { "prog", "-verbose", "-v", 0 };
	optind = 0;
	CHECK(getopt_long_only(3, av4, "v", lo, 0) == 'v' && getopt_long_only(3, av4, "v", lo, 0) == 'v');
	opterr = 1;
}

static void patterns(void)
{
	CHECK(fnmatch("*.c", "main.c", 0) == 0);
	CHECK(fnmatch("*.c", "dir/main.c", FNM_PATHNAME) == FNM_NOMATCH);
	CHECK(fnmatch("*/*.c", "dir/main.c", FNM_PATHNAME) == 0);
	CHECK(fnmatch("*", ".hidden", FNM_PERIOD) == FNM_NOMATCH);
	CHECK(fnmatch(".*", ".hidden", FNM_PERIOD) == 0);
	CHECK(fnmatch("[a-c]?[!x]", "bqy", 0) == 0);
	CHECK(fnmatch("[[:digit:]][[:upper:]]", "7Q", 0) == 0);
	CHECK(fnmatch("[]]", "]", 0) == 0);
	CHECK(fnmatch("\\*", "*", 0) == 0 && fnmatch("\\*", "x", 0) == FNM_NOMATCH);
	CHECK(fnmatch("ABC", "abc", FNM_CASEFOLD) == 0);
	CHECK(fnmatch("dir", "dir/sub/file", FNM_LEADING_DIR) == 0);
	CHECK(fnmatch("[", "[", 0) == 0);
	CHECK(fnmatch("[[.a.]]", "a", 0) == 0);
	/* no exponential blow-up */
	char s[200];
	memset(s, 'a', 199);
	s[199] = 0;
	CHECK(fnmatch("*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b", s, 0) == FNM_NOMATCH);

	char b1[] = "/usr/lib/", b2[] = "/usr/lib/", b3[] = "file", b4[] = "/", b5[] = "a/b//";
	CHECK(!strcmp(basename(b1), "lib") && !strcmp(dirname(b2), "/usr"));
	CHECK(!strcmp(basename(b3), "file") && !strcmp(dirname(b3), "."));
	CHECK(!strcmp(basename(b4), "/") && !strcmp(dirname(b5), "a"));
	CHECK(!strcmp(basename(0), ".") && !strcmp(dirname(""), "."));
}

static void users(void)
{
	struct passwd *pw = getpwuid(0);
	CHECK(pw && !strcmp(pw->pw_name, "root") && pw->pw_uid == 0 && pw->pw_dir[0] == '/');
	pw = getpwnam("root");
	CHECK(pw && pw->pw_uid == 0);
	CHECK(getpwnam("citadel-no-such-user") == 0);
	struct passwd p, *r;
	char small[4];
	CHECK(getpwuid_r(0, &p, small, sizeof small, &r) == ERANGE && !r);
	char big[1024];
	CHECK(getpwuid_r(0, &p, big, sizeof big, &r) == 0 && r == &p && !strcmp(p.pw_name, "root"));
	struct group *gr = getgrgid(0);
	CHECK(gr && gr->gr_gid == 0 && gr->gr_mem);
	CHECK(getgrnam("citadel-no-such-group") == 0);
	int n = 0;
	setpwent();
	while (getpwent())
		n++;
	endpwent();
	CHECK(n >= 1);
}

static void net(void)
{
	unsigned char a[16];
	char s[64];
	CHECK(inet_pton(AF_INET, "192.168.1.20", a) == 1 && a[0] == 192 && a[3] == 20);
	CHECK(inet_pton(AF_INET, "192.168.01.20", a) == 0 && inet_pton(AF_INET, "1.2.3.256", a) == 0);
	CHECK(inet_pton(AF_INET6, "2001:db8::1", a) == 1 && a[0] == 0x20 && a[15] == 1);
	CHECK(!strcmp(inet_ntop(AF_INET6, a, s, sizeof s), "2001:db8::1"));
	CHECK(inet_pton(AF_INET6, "::ffff:10.0.0.1", a) == 1 && !strcmp(inet_ntop(AF_INET6, a, s, sizeof s), "::ffff:10.0.0.1"));
	CHECK(inet_pton(AF_INET6, "1:0:0:2:0:0:0:3", a) == 1 && !strcmp(inet_ntop(AF_INET6, a, s, sizeof s), "1:0:0:2::3"));
	CHECK(inet_pton(AF_INET6, "1::2::3", a) == 0 && inet_pton(AF_INET6, "12345::", a) == 0);
	CHECK(inet_ntop(AF_INET6, a, s, 5) == 0 && errno == ENOSPC);
	CHECK(inet_pton(99, "x", a) == -1 && errno == EAFNOSUPPORT);
	struct in_addr ia;
	CHECK(inet_aton("127.1", &ia) == 1 && ntohl(ia.s_addr) == 0x7f000001);
	CHECK(inet_aton("0x7f.0.0.01", &ia) == 1 && ntohl(ia.s_addr) == 0x7f000001);
	CHECK(inet_addr("1.2.3.4") == htonl(0x01020304) && inet_addr("x") == INADDR_NONE);
	CHECK(!strcmp(inet_ntoa(ia), "127.0.0.1"));
	CHECK(htons(0x1234) == 0x3412 && (htonl)(1) == 0x01000000);

	/* TCP over loopback */
	int ls = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
	int one = 1;
	CHECK(setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) == 0);
	CHECK(bind(ls, (struct sockaddr *)&sa, sizeof sa) == 0 && listen(ls, 1) == 0);
	socklen_t sl = sizeof sa;
	CHECK(getsockname(ls, (struct sockaddr *)&sa, &sl) == 0 && sa.sin_port != 0);
	int cs = socket(AF_INET, SOCK_STREAM, 0);
	CHECK(connect(cs, (struct sockaddr *)&sa, sizeof sa) == 0);
	int as = accept(ls, 0, 0);
	CHECK(as >= 0 && send(cs, "ping", 4, 0) == 4);
	char buf[8] = { 0 };
	CHECK(recv(as, buf, sizeof buf, 0) == 4 && !memcmp(buf, "ping", 4));
	close(as);
	close(cs);
	close(ls);

	/* passing a descriptor over a UNIX socket */
	int sp[2], pp[2];
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0 && pipe(pp) == 0);
	char cbuf[CMSG_SPACE(sizeof(int))];
	memset(cbuf, 0, sizeof cbuf);
	struct iovec iov = { "x", 1 };
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
	struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &pp[1], sizeof(int));
	CHECK(sendmsg(sp[0], &m, 0) == 1);
	memset(cbuf, 0, sizeof cbuf);
	char rb;
	struct iovec riov = { &rb, 1 };
	struct msghdr rm = { .msg_iov = &riov, .msg_iovlen = 1, .msg_control = cbuf, .msg_controllen = sizeof cbuf };
	CHECK(recvmsg(sp[1], &rm, 0) == 1);
	cm = CMSG_FIRSTHDR(&rm);
	int got = -1;
	CHECK(cm && cm->cmsg_type == SCM_RIGHTS && !CMSG_NXTHDR(&rm, cm));
	if (cm)
		memcpy(&got, CMSG_DATA(cm), sizeof got);
	CHECK(got >= 0 && write(got, "z", 1) == 1 && read(pp[0], &rb, 1) == 1 && rb == 'z');
	close(got);
	close(sp[0]);
	close(sp[1]);

	/* poll, select, epoll, eventfd */
	struct pollfd pf = { pp[0], POLLIN, 0 };
	CHECK(poll(&pf, 1, 0) == 0);
	write(pp[1], "a", 1);
	CHECK(poll(&pf, 1, 100) == 1 && (pf.revents & POLLIN));
	fd_set rs;
	FD_ZERO(&rs);
	FD_SET(pp[0], &rs);
	struct timeval tv = { 0, 0 };
	CHECK(select(pp[0] + 1, &rs, 0, 0, &tv) == 1 && FD_ISSET(pp[0], &rs));
	CHECK(select(FD_SETSIZE + 1, &rs, 0, 0, &tv) == -1 && errno == EINVAL);
	int ep = epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event ev = { .events = EPOLLIN, .data.fd = pp[0] }, out;
	CHECK(ep >= 0 && epoll_ctl(ep, EPOLL_CTL_ADD, pp[0], &ev) == 0);
	CHECK(epoll_wait(ep, &out, 1, 100) == 1 && out.data.fd == pp[0]);
	int ef = eventfd(0, EFD_CLOEXEC);
	eventfd_t val = 0;
	CHECK(eventfd_write(ef, 5) == 0 && eventfd_write(ef, 2) == 0 && eventfd_read(ef, &val) == 0 && val == 7);
	close(ef);
	close(ep);
	close(pp[0]);
	close(pp[1]);
}

static void wide(void)
{
	CHECK(iswalpha(L'a') && iswalpha(0xe9) && iswalpha(0x4e2d) && !iswalpha(L'1') && !iswalpha(0x2603));
	CHECK(iswupper(0xc9) && iswlower(0xe9) && towupper(0xe9) == 0xc9 && towlower(0x3a3) == 0x3c3);
	CHECK(towupper(0x1c5) == 0x1c4 && towlower(0x1c5) == 0x1c6); /* title case Dž */
	CHECK(towupper(L'z') == L'Z' && towlower(0x2603) == 0x2603);
	CHECK(iswspace(0x3000) && !iswspace(0xa0) && iswpunct(0x2603) && iswdigit(L'7') && !iswdigit(0x0663));
	CHECK(wctype("alpha") && iswctype(L'x', wctype("alpha")) && !wctype("nonsense"));
	CHECK(wcwidth(L'a') == 1 && wcwidth(0x4e2d) == 2 && wcwidth(0x301) == 0 && wcwidth(0x7) == -1 && wcwidth(0) == 0);
	CHECK(wcwidth(0x1f600) == 2 && wcwidth(0xad) == 1 && wcwidth(0x200b) == 0);
	CHECK(wcswidth(L"a\x4e2d\x301", 3) == 3 && wcswidth(L"a\x7", 2) == -1);
	wchar_t *e;
	CHECK(wcstol(L"  -42abc", &e, 10) == -42 && *e == L'a');
	CHECK(wcstoul(L"ff", &e, 16) == 255);
	CHECK(setlocale(LC_ALL, "") && !strcmp(setlocale(LC_ALL, 0), "C") && setlocale(LC_ALL, "C.UTF-8"));
	CHECK(setlocale(LC_ALL, "de_DE.UTF-8") == 0 && !strcmp(localeconv()->decimal_point, "."));
}

static void system_info(void)
{
	struct utsname u;
	CHECK(uname(&u) == 0 && !strcmp(u.sysname, "Linux"));
	char h[256];
	CHECK(gethostname(h, sizeof h) == 0 && !strcmp(h, u.nodename));
	CHECK(gethostname(h, 1) == -1 && errno == ENAMETOOLONG);
	struct rlimit rl;
	CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 0);
	CHECK(sysconf(_SC_PAGESIZE) == 4096 && sysconf(_SC_NPROCESSORS_ONLN) >= 1 && sysconf(_SC_OPEN_MAX) == (long)rl.rlim_cur);
	CHECK(sysconf(12345) == -1 && errno == EINVAL);
	cpu_set_t cs;
	CHECK(sched_getaffinity(0, sizeof cs, &cs) == 0 && CPU_COUNT(&cs) >= 1);
	CHECK(sched_getcpu() >= 0 && sched_yield() == 0);
	struct statvfs sv;
	CHECK(statvfs("/", &sv) == 0 && sv.f_bsize > 0 && sv.f_namemax > 0);
	struct rusage ru;
	CHECK(getrusage(RUSAGE_SELF, &ru) == 0);
	unsigned char r[32] = { 0 };
	CHECK(getentropy(r, sizeof r) == 0 && getentropy(r, 257) == -1);
	double la[3];
	CHECK(getloadavg(la, 3) == 3 && la[0] >= 0);
	struct termios t;
	int fd = open("/dev/null", O_RDWR);
	CHECK(tcgetattr(fd, &t) == -1 && errno == ENOTTY && !isatty(fd) && ttyname(fd) == 0);
	close(fd);
	cfmakeraw(&t);
	CHECK(cfsetospeed(&t, B38400) == 0 && cfgetospeed(&t) == B38400);
	CHECK(syscall(SYS_getpid) == getpid());
	int *z = recallocarray(0, 0, 4, sizeof(int));
	z[3] = 9;
	z = recallocarray(z, 4, 8, sizeof(int));
	CHECK(z && z[3] == 9 && z[7] == 0);
	free(z);
}

int main(void)
{
	dirs();
	options();
	patterns();
	users();
	net();
	wide();
	system_info();
	return t_done();
}
