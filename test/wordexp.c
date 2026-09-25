/* <wordexp.h> */
#include <wordexp.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "harness.h"

/* expand s and compare with the words in want, separated by '|' */
static void t(const char *s, int flags, int ret, const char *want, int line)
{
	wordexp_t w;
	int r = wordexp(s, &w, flags);
	char got[512] = "";
	if (!r) {
		for (size_t i = 0; i < w.we_wordc; i++) {
			if (i)
				strcat(got, "|");
			strncat(got, w.we_wordv[i], sizeof got - strlen(got) - 2);
		}
		CHECK(w.we_wordv[w.we_wordc] == 0);
		wordfree(&w);
	}
	if (r != ret || (!r && strcmp(got, want))) {
		t_fail(__FILE__, line, s);
		t_puts("  got ");
		t_putl(r);
		t_puts(" <");
		t_puts(got);
		t_puts(">\n");
	}
}
#define T(s, want) t(s, 0, 0, want, __LINE__)
#define TF(s, fl, ret, want) t(s, fl, ret, want, __LINE__)

int main(void)
{
	setenv("HOME", "/home/test", 1);
	setenv("a", "hello world", 1);
	setenv("e", "", 1);
	setenv("p", "a,b,,c", 1);
	setenv("path", "/usr/lib/libc.c", 1);
	setenv("n", "5", 1);
	unsetenv("unset");
	unsetenv("IFS");

	/* quoting and splitting */
	T("a b   c", "a|b|c");
	T("  x  ", "x");
	T("'single q' \"double q\"", "single q|double q");
	T("\"a\"'b'c", "abc");
	T("\"\" ''", "|");
	T("a\\ b a\\\\b", "a b|a\\b");
	T("\"a\\$b\\\"c\\\\d\\e\"", "a$b\"c\\d\\e");
	T("'$a'", "$a");

	/* parameters */
	T("$a", "hello|world");
	T("\"$a\"", "hello world");
	T("x$a", "xhello|world");
	T("$e", "");
	T("\"$e\"", "");
	T("x\"$e\"y", "xy");
	T("$unset", "");
	T("\"$a\"$a", "hello worldhello|world");
	T("${#a} ${#unset}", "11|0");
	T("${a:-d} ${e:-d} ${unset-d}", "hello|world|d|d");
	T("${e-d}", "");
	T("${a:+alt} ${e:+alt} ${e+alt}", "alt|alt");
	T("${unset:-'$a'} \"${unset:-$a}\"", "$a|hello world");
	T("${path%/*} ${path%%/*}x ${path#*/} ${path##*/} ${path%\".c\"}", "/usr/lib|x|usr/lib/libc.c|libc.c|/usr/lib/libc");
	T("${newvar:=assigned} $newvar", "assigned|assigned");
	TF("${unset:?oops}", 0, WRDE_BADVAL, "");
	TF("$unset", WRDE_UNDEF, WRDE_BADVAL, "");
	TF("${unset:-ok}", WRDE_UNDEF, 0, "ok");
	T("$ a$ \\$a", "$|a$|$a");
	T("$# $1 $@", "0");

	/* IFS */
	setenv("IFS", " ,", 1);
	T("$p", "a|b||c");
	setenv("r", " , x ,, y ,", 1);
	T("$r", "|x||y");
	T("\"$p\"", "a,b,,c");
	setenv("IFS", "", 1);
	T("$a", "hello world");
	unsetenv("IFS");

	/* arithmetic */
	T("$((1+2*3)) $(( (1+2)*3 )) $((n*2)) $(($n-10))", "7|9|10|-5");
	T("$((7/2)) $((7%3)) $((-7/2)) $((1<<4)) $((255>>4))", "3|1|-3|16|15");
	T("$((5&3)) $((5|3)) $((5^3)) $((~0)) $((!0))", "1|7|6|-1|1");
	T("$((1<2)) $((2<=1)) $((3==3)) $((3!=3)) $((1&&0)) $((0||2))", "1|0|1|0|0|1");
	T("$((1?7:8)) $((0?7:8)) $((0x10)) $((010))", "7|8|16|8");
	T("$((9223372036854775807+1)) $(( (-9223372036854775807-1)/-1 ))", "-9223372036854775808|-9223372036854775808");
	TF("$((1/0))", 0, WRDE_SYNTAX, "");
	TF("$((1+))", 0, WRDE_SYNTAX, "");
	TF("$((1<<64))", 0, WRDE_SYNTAX, "");

	/* tilde */
	T("~ ~/x x~ \"~\" '~'", "/home/test|/home/test/x|x~|~|~");
	T("~nosuchuser_citadel", "~nosuchuser_citadel");

	/* errors */
	static const char *bad[] = { "a|b", "a;b", "a&b", "a>b", "a<b", "(a)", "{a}", "a\nb" };
	for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
		TF(bad[i], 0, WRDE_BADCHAR, "");
	T("\"a|b;c\" 'x&y' \\|", "a|b;c|x&y||");
	static const char *syn[] = { "'open", "\"open", "${open", "$(open", "`open", "${}", "${a b}", "\\" };
	for (size_t i = 0; i < sizeof syn / sizeof *syn; i++)
		TF(syn[i], 0, WRDE_SYNTAX, "");

	/* command substitution, and WRDE_NOCMD refusing it wherever it hides */
	if (access("/bin/sh", X_OK) == 0) {
		T("$(echo hi there) \"$(echo 'x  y')\" `echo back`", "hi|there|x  y|back");
		T("$(echo \"a)b\") $(echo $(echo nested)) $(printf 'a\\n\\n\\n')x", "a)b|nested|ax");
		T("$((1 + $(echo 2)))", "3");
	}
	static const char *cmds[] = { "$(id)", "`id`", "\"$(id)\"", "\"`id`\"", "${unset:-$(id)}", "${a:-`id`}",
	                              "$(( $(id) ))", "${a#`id`}", "x${unset:=$(id)}" };
	for (size_t i = 0; i < sizeof cmds / sizeof *cmds; i++)
		TF(cmds[i], WRDE_NOCMD, WRDE_CMDSUB, "");
	TF("'$(id)' \"\\$(id)\" \\`id\\` $((1+1))", WRDE_NOCMD, 0, "$(id)|$(id)|`id`|2");

	/* deep nesting is refused rather than exhausting the stack */
	static char deep[400000];
	char *q = deep;
	q += sprintf(q, "$((");
	for (int i = 0; i < 50000; i++)
		*q++ = '(';
	strcpy(q, "1");
	TF(deep, WRDE_NOCMD, WRDE_SYNTAX, "");
	q = deep;
	for (int i = 0; i < 50000; i++)
		q += sprintf(q, "${x:-");
	TF(deep, WRDE_NOCMD, WRDE_SYNTAX, "");

	/* pathname expansion */
	char dir[] = "/tmp/citadel-wordexpXXXXXX";
	CHECK(mkdtemp(dir) != 0);
	char cwd[256];
	CHECK(getcwd(cwd, sizeof cwd) != 0);
	CHECK(chdir(dir) == 0);
	static const char *files[] = { "f1", "f2", "g1" };
	for (int i = 0; i < 3; i++)
		close(open(files[i], O_CREAT | O_WRONLY, 0600));
	setenv("star", "f*", 1);
	T("f* \"f*\" 'f*' f\\* z* [fg]1", "f1|f2|f*|f*|f*|z*|f1|g1");
	T("$star \"$star\"", "f1|f2|f*");
	for (int i = 0; i < 3; i++)
		unlink(files[i]);
	CHECK(chdir(cwd) == 0);
	rmdir(dir);

	/* flags */
	wordexp_t w;
	w.we_offs = 2;
	CHECK(wordexp("a b", &w, WRDE_DOOFFS) == 0);
	CHECK(w.we_wordc == 2 && !w.we_wordv[0] && !w.we_wordv[1] && !strcmp(w.we_wordv[2], "a"));
	CHECK(wordexp("c", &w, WRDE_DOOFFS | WRDE_APPEND) == 0);
	CHECK(w.we_wordc == 3 && !strcmp(w.we_wordv[4], "c") && !w.we_wordv[5]);
	CHECK(wordexp("d e", &w, WRDE_DOOFFS | WRDE_REUSE) == 0);
	CHECK(w.we_wordc == 2 && !strcmp(w.we_wordv[3], "e"));
	CHECK(wordexp("x|y", &w, WRDE_APPEND | WRDE_DOOFFS) == WRDE_BADCHAR);
	CHECK(w.we_wordc == 2); /* untouched on error */
	wordfree(&w);
	return t_done();
}
