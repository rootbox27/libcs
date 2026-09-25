/* <glob.h> on a scratch directory tree */
#include <glob.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "harness.h"

static char root[] = "/tmp/citadel-glob-XXXXXX";

static void mk(const char *p, int dir)
{
	if (dir)
		mkdir(p, 0700);
	else
		close(open(p, O_CREAT | O_WRONLY, 0600));
}

static int is(const char *pat, int fl, const char *want)
{
	glob_t g;
	char got[512] = "";
	int r = glob(pat, fl, 0, &g);
	if (r) {
		strcpy(got, r == GLOB_NOMATCH ? "NOMATCH" : "ERR");
	} else {
		for (size_t i = 0; i < g.gl_pathc; i++) {
			if (i)
				strcat(got, " ");
			strcat(got, g.gl_pathv[i]);
		}
		globfree(&g);
	}
	if (strcmp(got, want)) {
		t_puts(pat);
		t_puts(": ");
		t_puts(got);
		t_puts("\n");
		return 0;
	}
	return 1;
}

int main(void)
{
	CHECK(mkdtemp(root) != 0);
	CHECK(chdir(root) == 0);
	mk("a", 1); mk("a/b", 1); mk("a/c", 1); mk(".hid", 1);
	mk("a/b/f1.c", 0); mk("a/b/f2.h", 0); mk("a/c/f3.c", 0); mk(".dot", 0); mk("top.txt", 0); mk("st*ar", 0);
	symlink("a", "lnk");

	CHECK(is("*", 0, "a lnk st*ar top.txt"));
	CHECK(is(".*", 0, ". .. .dot .hid"));
	CHECK(is("a/*/*.c", 0, "a/b/f1.c a/c/f3.c"));
	CHECK(is("*/*/f?.*", 0, "a/b/f1.c a/b/f2.h a/c/f3.c lnk/b/f1.c lnk/b/f2.h lnk/c/f3.c"));
	CHECK(is("[a-c]", 0, "a"));
	CHECK(is("[!a]*", 0, "lnk st*ar top.txt"));
	CHECK(is("*", GLOB_MARK, "a/ lnk/ st*ar top.txt"));
	CHECK(is("*", GLOB_ONLYDIR, "a lnk"));
	CHECK(is("st\\*ar", 0, "st*ar"));
	CHECK(is("st\\*ar", GLOB_NOESCAPE, "NOMATCH"));
	CHECK(is("nope*", 0, "NOMATCH"));
	CHECK(is("nope*", GLOB_NOCHECK, "nope*"));
	CHECK(is("missing", GLOB_NOMAGIC, "missing"));
	CHECK(is("missing", 0, "NOMATCH"));
	CHECK(is("top.txt", 0, "top.txt"));
	CHECK(is("a/{b,c}/*.c", GLOB_BRACE, "a/b/f1.c a/c/f3.c"));
	CHECK(is("{top,a/b/f1}.{txt,c}", GLOB_BRACE, "top.txt a/b/f1.c"));
	CHECK(is("a//b/*", 0, "a//b/f1.c a//b/f2.h"));

	char *home = getenv("HOME");
	glob_t g;
	if (home && *home) {
		CHECK(glob("~", GLOB_TILDE, 0, &g) == 0 && g.gl_pathc == 1 && !strcmp(g.gl_pathv[0], home));
		globfree(&g);
	}
	CHECK(glob("~no-such-user-xyz/x", GLOB_TILDE_CHECK, 0, &g) == GLOB_NOMATCH);

	/* offsets and appending */
	g.gl_offs = 2;
	CHECK(glob("a/b/*", GLOB_DOOFFS, 0, &g) == 0);
	CHECK(glob("top*", GLOB_DOOFFS | GLOB_APPEND, 0, &g) == 0);
	CHECK(g.gl_pathc == 3 && !g.gl_pathv[0] && !g.gl_pathv[1] && !strcmp(g.gl_pathv[4], "top.txt") && !g.gl_pathv[5]);
	globfree(&g);

	/* clean up */
	const char *files[] = { "a/b/f1.c", "a/b/f2.h", "a/c/f3.c", ".dot", "top.txt", "st*ar", "lnk", 0 };
	for (int i = 0; files[i]; i++)
		unlink(files[i]);
	rmdir("a/b"); rmdir("a/c"); rmdir("a"); rmdir(".hid");
	chdir("/");
	CHECK(rmdir(root) == 0);
	return t_done();
}
