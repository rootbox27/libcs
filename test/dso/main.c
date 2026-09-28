/* Shared libraries (dynamic linking stage 2). Its stdout, checked against
 * test/dso_main.expected, shows the constructor and destructor order. */
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "harness.h"

int ta_value(void);
const char *ta_calls_hook(void);
int tb_bump(void);
int *tb_gd_addr(void);
int *tb_ie_addr(void);
int *tb_local_addr(void);
int tb_has_missing(void);
int tb_ptr_ok(void);
extern int tb_counter;       /* copied into the program */
extern __thread int ta_tls;  /* another module's TLS, used directly */

extern char **environ;

/* called by the libraries' constructors and destructors */
void log_event(const char *s)
{
	write(1, s, strlen(s));
	write(1, "\n", 1);
}

/* overrides libta's hook: the program comes first in the search order */
const char *hook(void)
{
	return "main";
}

struct seen {
	int ta, tb, libc;
};

static int visit(struct dl_phdr_info *i, size_t size, void *arg)
{
	struct seen *s = arg;
	if (strstr(i->dlpi_name, "/libta.so"))
		s->ta = 1;
	if (strstr(i->dlpi_name, "/libtb.so"))
		s->tb = 1;
	if (strstr(i->dlpi_name, "libc.so"))
		s->libc = 1;
	return 0;
}

static void *thread_fn(void *arg)
{
	int *r = arg;
	/* a new thread starts with every library's initial TLS values */
	r[0] = *tb_gd_addr() == 11 && *tb_ie_addr() == 22 && *tb_local_addr() == 33 && ta_tls == 44 && tb_ptr_ok();
	*tb_gd_addr() = 1000;
	ta_tls = 2000;
	r[2] = (int)(long)tb_gd_addr() != 0;
	return 0;
}

static int run_argv(char **argv, char **env)
{
	pid_t pid;
	const char *path = argv[0];
	int fd = open("/dev/null", O_WRONLY);
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fd, 2);
	if (posix_spawn(&pid, path, &fa, 0, argv, env))
		return -1;
	posix_spawn_file_actions_destroy(&fa);
	close(fd);
	int st;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int run(const char *path, char **env)
{
	char *argv[] = { (char *)path, 0 };
	return run_argv(argv, env);
}

int main(int argc, char **argv)
{
	log_event("main");
	CHECK(ta_value() == 21);
	CHECK(!strcmp(ta_calls_hook(), "main"));
	CHECK(tb_counter == 100 && tb_bump() == 101 && tb_counter == 101);
	CHECK(*tb_gd_addr() == 11 && *tb_ie_addr() == 22 && *tb_local_addr() == 33 && ta_tls == 44);
	CHECK(!tb_has_missing());
	CHECK(tb_ptr_ok());

	int r[3] = { 0 };
	pthread_t t;
	CHECK(pthread_create(&t, 0, thread_fn, r) == 0 && pthread_join(t, 0) == 0);
	CHECK(r[0] && r[2]);
	/* the thread's writes went to its own copies */
	CHECK(*tb_gd_addr() == 11 && ta_tls == 44);

	struct seen s = { 0 };
	dl_iterate_phdr(visit, &s);
	CHECK(s.ta && s.tb && s.libc);

	/* helpers next to this program */
	char dir[4096], path[4200], env[4300];
	const char *slash = strrchr(argv[0], '/');
	size_t dl = slash ? (size_t)(slash - argv[0]) : 1;
	memcpy(dir, slash ? argv[0] : ".", dl);
	dir[dl] = 0;
	/* a library that cannot be found: a message and 127 */
	strcpy(path, dir);
	strcat(path, "/dso_missing");
	CHECK(run(path, environ) == 127);
	/* LD_LIBRARY_PATH comes before DT_RUNPATH */
	strcpy(path, dir);
	strcat(path, "/dso_alt");
	CHECK(run(path, environ) == 2);
	strcpy(env, "LD_LIBRARY_PATH=");
	strcat(env, dir);
	strcat(env, "/alt");
	char *altenv[] = { env, 0 };
	CHECK(run(path, altenv) == 3);
	/* LD_PRELOAD comes first; its soname satisfies DT_NEEDED */
	strcpy(env, "LD_PRELOAD=");
	strcat(env, dir);
	strcat(env, "/alt/libtb.so");
	CHECK(run(path, altenv) == 3);
	/* libc.so run as a program runs the one named by its argument */
	char *direct[] = { LIBC_SO, path, 0 };
	CHECK(run_argv(direct, environ) == 2);
	char *list[] = { LIBC_SO, "--list", path, 0 };
	CHECK(run_argv(list, environ) == 0);
	char *usage[] = { LIBC_SO, 0 };
	CHECK(run_argv(usage, environ) == 1);
	return t_done();
}
