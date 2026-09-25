/* getopt (POSIX) and getopt_long / getopt_long_only (GNU). getopt stops
 * at the first non-option; getopt_long permutes non-options to the end
 * unless the option string starts with '+' or '-' or POSIXLY_CORRECT is
 * set. Setting optind to 0 restarts scanning. */
#include "internal.h"
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1, opterr = 1, optopt;
static int optpos;

static void complain(const char *prog, const char *msg, const char *opt, size_t l)
{
	if (!opterr)
		return;
	flockfile(stderr);
	fprintf(stderr, "%s: %s: ", prog, msg);
	fwrite(opt, 1, l, stderr);
	putc('\n', stderr);
	funlockfile(stderr);
}

int getopt(int argc, char *const argv[], const char *optstring)
{
	optarg = 0;
	if (!optind) {
		optind = 1;
		optpos = 0;
	}
	if (optind >= argc || !argv[optind])
		return -1;
	const char *arg = argv[optind];
	if (!optpos) {
		if (arg[0] != '-') {
			if (optstring[0] == '-') {
				optarg = argv[optind++];
				return 1;
			}
			return -1;
		}
		if (!arg[1])
			return -1;          /* "-" is an operand */
		if (arg[1] == '-' && !arg[2]) {
			optind++;           /* "--" ends options */
			return -1;
		}
		optpos = 1;
	}
	if (optstring[0] == '-' || optstring[0] == '+')
		optstring++;
	int c = (unsigned char)arg[optpos++];
	const char *d = c != ':' ? strchr(optstring, c) : 0;
	if (!arg[optpos]) {
		optind++;
		optpos = 0;
	}
	if (!d) {
		optopt = c;
		if (optstring[0] != ':') {
			char ch = (char)c;
			complain(argv[0], "unrecognized option", &ch, 1);
		}
		return '?';
	}
	if (d[1] != ':')
		return c;
	/* argument: attached, optional, or the next word */
	optarg = 0;
	if (optpos) {
		optarg = (char *)arg + optpos;
		optind++;
		optpos = 0;
		return c;
	}
	if (d[2] == ':')
		return c;
	if (optind >= argc) {
		optopt = c;
		if (optstring[0] == ':')
			return ':';
		char ch = (char)c;
		complain(argv[0], "option requires an argument", &ch, 1);
		return '?';
	}
	optarg = argv[optind++];
	return c;
}

/* Move argv[src] down to position dest, shifting the others up. */
static void permute(char *const *argv, int dest, int src)
{
	char **av = (char **)argv;
	char *tmp = av[src];
	for (int i = src; i > dest; i--)
		av[i] = av[i - 1];
	av[dest] = tmp;
}

static int long_core(int argc, char *const *argv, const char *optstring, const struct option *lo, int *idx, int longonly)
{
	const char *arg = argv[optind];
	const char *os = optstring + (optstring[0] == '+' || optstring[0] == '-');
	if (optind >= argc || !arg || optpos || arg[0] != '-' ||
	    !((arg[1] == '-' && arg[2]) || (longonly && arg[1] && arg[1] != '-')))
		return getopt(argc, argv, optstring);
	/* getopt_long_only: "-x" for a short option x is that option */
	if (longonly && arg[1] != '-' && !arg[2] && strchr(os, arg[1]))
		return getopt(argc, argv, optstring);

	const char *name = arg + 1 + (arg[1] == '-');
	const char *eq = strchrnul(name, '=');
	size_t nl = (size_t)(eq - name);
	int match = -1, cnt = 0;
	for (int i = 0; lo[i].name; i++) {
		if (strncmp(lo[i].name, name, nl))
			continue;
		if (!lo[i].name[nl]) {
			match = i;
			cnt = 1;
			break;
		}
		if (match < 0 || lo[i].has_arg != lo[match].has_arg || lo[i].flag != lo[match].flag || lo[i].val != lo[match].val)
			cnt++;
		if (match < 0)
			match = i;
	}
	/* getopt_long_only: no long match at all, but a valid short option */
	if (longonly && arg[1] != '-' && !cnt && strchr(os, name[0]))
		return getopt(argc, argv, optstring);
	int colon = os[0] == ':';
	optind++;
	if (cnt != 1) {
		optopt = 0;
		complain(argv[0], cnt ? "option is ambiguous" : "unrecognized option", arg, strlen(arg));
		return '?';
	}
	const struct option *o = &lo[match];
	optarg = 0;
	if (*eq) {
		if (o->has_arg == no_argument) {
			optopt = o->val;
			complain(argv[0], "option does not take an argument", arg, (size_t)(eq - arg));
			return '?';
		}
		optarg = (char *)eq + 1;
	} else if (o->has_arg == required_argument) {
		if (optind >= argc) {
			optopt = o->val;
			complain(argv[0], "option requires an argument", arg, strlen(arg));
			return colon ? ':' : '?';
		}
		optarg = argv[optind++];
	}
	if (idx)
		*idx = match;
	if (o->flag) {
		*o->flag = o->val;
		return 0;
	}
	return o->val;
}

static int long_permute(int argc, char *const *argv, const char *optstring, const struct option *lo, int *idx, int longonly)
{
	optarg = 0;
	if (!optind) {
		optind = 1;
		optpos = 0;
	}
	if (optind >= argc || !argv[optind])
		return -1;
	int skipped = optind;
	if (!optpos && optstring[0] != '+' && optstring[0] != '-' && !getenv("POSIXLY_CORRECT")) {
		int i = optind;
		for (;; i++) {
			if (i >= argc || !argv[i])
				return -1;
			if (argv[i][0] == '-' && argv[i][1])
				break;
		}
		optind = i;
	}
	int resumed = optind;
	int r = long_core(argc, argv, optstring, lo, idx, longonly);
	if (resumed > skipped) {
		/* move the option (and its argument) before the operands */
		int cnt = optind - resumed;
		for (int i = 0; i < cnt; i++)
			permute(argv, skipped, optind - 1);
		optind = skipped + cnt;
	}
	return r;
}

int getopt_long(int argc, char *const *argv, const char *optstring, const struct option *lo, int *idx)
{
	return long_permute(argc, argv, optstring, lo, idx, 0);
}

int getopt_long_only(int argc, char *const *argv, const char *optstring, const struct option *lo, int *idx)
{
	return long_permute(argc, argv, optstring, lo, idx, 1);
}
