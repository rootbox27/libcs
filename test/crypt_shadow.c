/* <crypt.h> and <shadow.h>. Vectors cross-checked against libxcrypt. */
#include <crypt.h>
#include <shadow.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

static const char *vec[][3] = {
	{ "Hello world!", "$6$saltstring",
	  "$6$saltstring$svn8UoSVapNtMuq1ukKS4tPQd8iKwSMHWjl/O817G3uBnIFNjnQJuesI68u4OTLiBFdcbYEdFCoEOfaS35inz1" },
	{ "Hello world!", "$5$saltstring", "$5$saltstring$5B8vYYiY.CVt1RlTTf8KbXBH3hsxY/GNooZaBBGWEc5" },
	{ "Hello world!", "$5$rounds=10000$saltstringsaltstring",
	  "$5$rounds=10000$saltstringsaltst$3xv.VbSHBb41AL9AvLeujZkZRBAwqFMz2.opqey6IcA" },
	{ "Hello world!", "$6$rounds=1400$anotherlongsaltstring",
	  "$6$rounds=1400$anotherlongsalts$5FGyu8c4BZDX4wJgs0Un26YOw2XibT5eTkHF1I1aP3QqStoJI9BHD2YPJYsAjEePVGUyBjdZxcNqMWlrrbIOC." },
	{ "U*U", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.", "$2a$05$CCCCCCCCCCCCCCCCCCCCC.E5YPO9kmyuRGyh0XouQYb4YMJKvyOeW" },
	{ "", "$2b$04$abcdefghijklmnopqrstuu", "$2b$04$abcdefghijklmnopqrstuubyCG3zY1GIXMyxfivm.ClDiInHzxjiq" },
	{ "correct horse", "$2y$06$0123456789abcdefghijkl", "$2y$06$0123456789abcdefghijkeGXdTx4w5qt26h7Arzs2NBQWRebGDFda" },
};

static void crypts(void)
{
	for (size_t i = 0; i < sizeof vec / sizeof *vec; i++) {
		CHECK_STR(crypt(vec[i][0], vec[i][1]), vec[i][2]);
		/* a full hash works as its own setting: that is how verification works */
		CHECK_STR(crypt(vec[i][0], vec[i][2]), vec[i][2]);
	}
	struct crypt_data cd = { 0 };
	CHECK_STR(crypt_r("U*U", vec[4][1], &cd), vec[4][2]);

	/* failures give a token that can never match a real hash */
	errno = 0;
	CHECK_STR(crypt("x", "ab"), "*0"); /* legacy DES is not supported */
	CHECK(errno == EINVAL);
	CHECK_STR(crypt("x", "*0"), "*1");
	CHECK_STR(crypt("x", "$1$abc$"), "*0");
	CHECK_STR(crypt("x", "$5$rounds=999$salt"), "*0");
	CHECK_STR(crypt("x", "$2b$03$abcdefghijklmnopqrstuu"), "*0");
	CHECK_STR(crypt("x", "$2b$04$abc"), "*0");
	CHECK_STR(crypt("x", "$6$sa:lt"), "*0");

	char big[600];
	memset(big, 'a', sizeof big - 1);
	big[sizeof big - 1] = 0;
	CHECK_STR(crypt(big, "$6$salt"), "*0");
}

static void shadow(void)
{
	struct spwd *sp = sgetspent("root:$6$x$y:19000:0:99999:7:::\n");
	CHECK(sp);
	if (sp) {
		CHECK_STR(sp->sp_namp, "root");
		CHECK_STR(sp->sp_pwdp, "$6$x$y");
		CHECK(sp->sp_lstchg == 19000 && sp->sp_min == 0 && sp->sp_max == 99999 && sp->sp_warn == 7);
		CHECK(sp->sp_inact == -1 && sp->sp_expire == -1 && sp->sp_flag == (unsigned long)-1);
	}
	CHECK(!sgetspent("bad:x:notanumber::::::"));
	CHECK(!sgetspent("nocolon"));

	char path[] = "/tmp/citadel-shadowXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	FILE *f = fdopen(fd, "w+");
	CHECK(f);
	fputs("# comment\n\nalice:!:1:2:3:4:5:6:\nbroken:x:zz\n", f);
	struct spwd b = { "bob", "$2b$04$abc", 10, -1, -1, -1, -1, 20000, (unsigned long)-1 };
	CHECK(putspent(&b, f) == 0);
	struct spwd bad = { "ev:il", "", -1, -1, -1, -1, -1, -1, (unsigned long)-1 };
	CHECK(putspent(&bad, f) == -1 && errno == EINVAL);
	rewind(f);
	sp = fgetspent(f);
	CHECK(sp && !strcmp(sp->sp_namp, "alice") && sp->sp_expire == 6);
	sp = fgetspent(f); /* the broken line is skipped */
	CHECK(sp && !strcmp(sp->sp_namp, "bob") && sp->sp_lstchg == 10 && sp->sp_min == -1 && sp->sp_expire == 20000);
	CHECK(!fgetspent(f));

	/* too small a buffer is ERANGE, not a truncated entry */
	rewind(f);
	char tiny[8];
	struct spwd s, *r;
	CHECK(fgetspent_r(f, &s, tiny, sizeof tiny, &r) == ERANGE && !r);
	fclose(f);
	unlink(path);

	/* names that could inject a field never match */
	char buf[256];
	CHECK(getspnam_r("root:x", &s, buf, sizeof buf, &r) == ENOENT || !r);
	CHECK(getspnam_r("", &s, buf, sizeof buf, &r) == ENOENT || !r);
	/* /etc/shadow may be unreadable here; either way no crash */
	setspent();
	getspent();
	endspent();
}

int main(void)
{
	crypts();
	shadow();
	return t_done();
}
