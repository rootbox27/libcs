/* <ftw.h> and <langinfo.h> */
#include <ftw.h>
#include <langinfo.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "harness.h"

static int files, dirs, post, slinks, maxlevel, order_ok = 1;
static int fn(const char *p, const struct stat *st, int t, struct FTW *f)
{
	(void)st;
	if (f->level > maxlevel)
		maxlevel = f->level;
	if (t == FTW_F)
		files++;
	if (t == FTW_D)
		dirs++;
	if (t == FTW_DP) {
		post++;
		if (files < 2 && !strcmp(p + f->base, "d"))
			order_ok = 0; /* post-order: contents first */
	}
	if (t == FTW_SL || t == FTW_SLN)
		slinks++;
	return 0;
}

static int stop(const char *p, const struct stat *st, int t)
{
	(void)st;
	(void)t;
	return strstr(p, "f2") ? 42 : 0;
}

int main(void)
{
	char root[] = "/tmp/citadel-ftw-XXXXXX";
	CHECK(mkdtemp(root) != 0);
	CHECK(chdir(root) == 0);
	mkdir("d", 0700);
	mkdir("d/e", 0700);
	close(open("d/f1", O_CREAT | O_WRONLY, 0600));
	close(open("d/e/f2", O_CREAT | O_WRONLY, 0600));
	symlink("e", "d/loop");
	symlink("nowhere", "d/dangling");

	CHECK(nftw("d", fn, 8, FTW_PHYS) == 0);
	CHECK(files == 2 && dirs == 2 && slinks == 2 && maxlevel == 2);
	files = dirs = slinks = 0;
	CHECK(nftw("d/", fn, 8, FTW_DEPTH) == 0);
	CHECK(files == 2 && post == 2 && slinks == 1 && order_ok); /* d/loop is e again */
	CHECK(ftw("d", stop, 4) == 42);

	unlink("d/e/f2");
	unlink("d/f1");
	unlink("d/loop");
	unlink("d/dangling");
	rmdir("d/e");
	rmdir("d");
	chdir("/");
	CHECK(rmdir(root) == 0);

	CHECK(!strcmp(nl_langinfo(CODESET), "UTF-8"));
	CHECK(!strcmp(nl_langinfo(DAY_1), "Sunday") && !strcmp(nl_langinfo(ABMON_12), "Dec"));
	CHECK(!strcmp(nl_langinfo(RADIXCHAR), ".") && !strcmp(nl_langinfo(D_FMT), "%m/%d/%y"));
	CHECK(!strcmp(nl_langinfo(YESEXPR), "^[yY]") && !strcmp(nl_langinfo(12345678), ""));
	return t_done();
}
