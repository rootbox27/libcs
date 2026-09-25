/* pseudo-terminals and getifaddrs */
#include <ifaddrs.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pty.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "harness.h"

int main(void)
{
	int m = posix_openpt(O_RDWR | O_NOCTTY);
	if (m < 0) {
		t_puts("no /dev/ptmx: skipping pty checks\n");
	} else {
		CHECK(grantpt(m) == 0 && unlockpt(m) == 0);
		char name[32];
		CHECK(ptsname_r(m, name, sizeof name) == 0 && !strncmp(name, "/dev/pts/", 9));
		CHECK(ptsname_r(m, name, 4) == ERANGE);
		close(m);
		CHECK(ptsname_r(0 + 1000, name, sizeof name) != 0);

		int s;
		CHECK(openpty(&m, &s, name, 0, 0) == 0 && isatty(m) && isatty(s));
		CHECK(write(m, "ping\n", 5) == 5);
		char buf[16] = { 0 };
		CHECK(read(s, buf, sizeof buf) == 5 && !strcmp(buf, "ping\n"));
		close(s);
		close(m);

		pid_t pid = forkpty(&m, 0, 0, 0);
		if (pid == 0) {
			/* child: controlling terminal is the pty */
			_exit(isatty(0) && isatty(1) && getsid(0) == getpid() ? 0 : 1);
		}
		CHECK(pid > 0);
		int st;
		CHECK(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
		close(m);
	}

	struct ifaddrs *ia;
	CHECK(getifaddrs(&ia) == 0);
	int lo_packet = 0, lo_inet = 0;
	for (struct ifaddrs *p = ia; p; p = p->ifa_next) {
		CHECK(p->ifa_name && p->ifa_addr);
		if (!strcmp(p->ifa_name, "lo") && p->ifa_addr->sa_family == AF_PACKET)
			lo_packet = (p->ifa_flags & IFF_LOOPBACK) != 0;
		if (!strcmp(p->ifa_name, "lo") && p->ifa_addr->sa_family == AF_INET)
			lo_inet = ((struct sockaddr_in *)p->ifa_addr)->sin_addr.s_addr == htonl(0x7f000001) && p->ifa_netmask;
	}
	CHECK(lo_packet && lo_inet);
	freeifaddrs(ia);
	return t_done();
}
