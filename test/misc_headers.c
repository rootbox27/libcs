/* mntent, utmp, dlfcn, execinfo, nl_types, ulimit and packet headers */
#include <dlfcn.h>
#include <execinfo.h>
#include <mntent.h>
#include <nl_types.h>
#include <stdlib.h>
#include <string.h>
#include <tar.h>
#include <cpio.h>
#include <ulimit.h>
#include <unistd.h>
#include <utmp.h>
#include <net/ethernet.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include "harness.h"

_Static_assert(sizeof(struct iphdr) == 20 && sizeof(struct ip) == 20, "ip header");
_Static_assert(sizeof(struct udphdr) == 8 && sizeof(struct icmphdr) == 8, "udp/icmp header");
_Static_assert(sizeof(struct ether_header) == 14, "ethernet header");

__attribute__((noinline)) static int depth2(void **b) { return backtrace(b, 8); }

int main(void)
{
	char tmp[] = "/tmp/citadel-mnt-XXXXXX";
	int fd = mkstemp(tmp);
	const char *text = "# comment\n/dev/sda1 / ext4 rw,relatime 0 1\nserver:/x /mnt/my\\040dir nfs ro,soft,timeo=5\n";
	CHECK(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	close(fd);
	FILE *f = setmntent(tmp, "r");
	struct mntent *m = getmntent(f);
	CHECK(m && !strcmp(m->mnt_dir, "/") && !strcmp(m->mnt_type, "ext4") && m->mnt_passno == 1);
	CHECK(hasmntopt(m, "relatime") && !hasmntopt(m, "rel"));
	m = getmntent(f);
	CHECK(m && !strcmp(m->mnt_dir, "/mnt/my dir") && hasmntopt(m, "timeo") && m->mnt_freq == 0);
	CHECK(!getmntent(f));
	endmntent(f);
	f = setmntent(tmp, "a+");
	struct mntent add = { "tmpfs", "/a b", "tmpfs", "defaults", 0, 0 };
	CHECK(addmntent(f, &add) == 0);
	endmntent(f);
	f = setmntent(tmp, "r");
	getmntent(f);
	getmntent(f);
	m = getmntent(f);
	CHECK(m && !strcmp(m->mnt_dir, "/a b"));
	endmntent(f);
	unlink(tmp);

	/* utmp: an empty private file */
	char ut[] = "/tmp/citadel-utmp-XXXXXX";
	close(mkstemp(ut));
	CHECK(utmpname(ut) == 0);
	setutent();
	CHECK(!getutent());
	struct utmp u;
	memset(&u, 0, sizeof u);
	u.ut_type = USER_PROCESS;
	strcpy(u.ut_id, "t1");
	strcpy(u.ut_line, "pts/9");
	strcpy(u.ut_user, "someone");
	CHECK(pututline(&u) != 0);
	setutent();
	struct utmp key;
	memset(&key, 0, sizeof key);
	strcpy(key.ut_line, "pts/9");
	struct utmp *got = getutline(&key);
	CHECK(got && !strcmp(got->ut_user, "someone"));
	endutent();
	unlink(ut);

	CHECK(!dlopen("libfoo.so", RTLD_NOW) && dlerror() && !dlerror());
	void *self = dlopen(0, RTLD_NOW);
	CHECK(self && !dlsym(self, "main") && dlclose(self) == 0);

	void *bt[8];
	int n = depth2(bt);
	CHECK(n >= 0 && n <= 8);
	char **syms = backtrace_symbols(bt, n);
	CHECK(!n || (syms && syms[0][0] == '0'));
	free(syms);

	CHECK(catopen("nothing", 0) == (nl_catd)-1);
	CHECK(!strcmp(catgets((nl_catd)-1, 1, 1, "default"), "default"));
	CHECK(ulimit(UL_GETFSIZE) > 0);
	CHECK(REGTYPE == '0' && C_ISDIR == 040000);
	return t_done();
}
