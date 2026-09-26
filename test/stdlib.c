#include "harness.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned calls;
static int cmp_int(const void *a, const void *b)
{
	calls++;
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}
static int cmp_rev(const void *a, const void *b, void *arg)
{
	(*(int *)arg)++;
	return cmp_int(b, a);
}
struct rec { char key[5]; unsigned char pad[7]; };
static int cmp_rec(const void *a, const void *b) { return strcmp(((const struct rec *)a)->key, ((const struct rec *)b)->key); }

static void sorting(void)
{
	static int a[20000];
	unsigned seed = 1;
	int patterns = 6;
	for (int pat = 0; pat < patterns; pat++) {
		for (int n = 0; n <= 20000; n = n < 40 ? n + 1 : n * 3) {
			for (int i = 0; i < n; i++) {
				switch (pat) {
				case 0: a[i] = rand_r(&seed); break;
				case 1: a[i] = i; break;
				case 2: a[i] = n - i; break;
				case 3: a[i] = rand_r(&seed) % 4; break;
				case 4: a[i] = 7; break;
				default: a[i] = i % 2 ? i : n - i; break; /* organ pipe-ish */
				}
			}
			long sum = 0;
			for (int i = 0; i < n; i++)
				sum += a[i];
			calls = 0;
			qsort(a, (size_t)n, sizeof *a, cmp_int);
			int ok = 1;
			long sum2 = 0;
			for (int i = 0; i < n; i++) {
				sum2 += a[i];
				if (i && a[i - 1] > a[i])
					ok = 0;
			}
			CHECK(ok && sum == sum2);
			/* no quadratic blow-up on any pattern */
			if (n >= 1000)
				CHECK(calls < 40u * (unsigned)n * (unsigned)(32 - __builtin_clz((unsigned)n)));
		}
	}
	int cnt = 0;
	int b[] = { 3, 1, 2 };
	qsort_r(b, 3, sizeof *b, cmp_rev, &cnt);
	CHECK(b[0] == 3 && b[1] == 2 && b[2] == 1 && cnt > 0);
	/* odd element size and alignment */
	struct rec r[50];
	for (int i = 0; i < 50; i++)
		snprintf(r[i].key, sizeof r[i].key, "%04d", (i * 37) % 50);
	qsort(r, 50, sizeof *r, cmp_rec);
	int ok = 1;
	for (int i = 0; i < 50; i++)
		ok &= atoi(r[i].key) == i;
	CHECK(ok);

	int s[100];
	for (int i = 0; i < 100; i++)
		s[i] = i * 2;
	for (int k = -1; k <= 200; k++) {
		int *f = bsearch(&k, s, 100, sizeof *s, cmp_int);
		CHECK(k >= 0 && k % 2 == 0 && k < 200 ? f && *f == k : !f);
	}
	CHECK(bsearch(&s[0], s, 0, sizeof *s, cmp_int) == 0);
}

static void environment(void)
{
	CHECK(getenv("CITADEL_TEST") && !strcmp(getenv("CITADEL_TEST"), "1"));
	CHECK(getenv("CITADEL_NOPE") == 0);
	CHECK(getenv("CITADEL_TEST=") == 0);
	CHECK(setenv("CT_A", "one", 0) == 0 && !strcmp(getenv("CT_A"), "one"));
	CHECK(setenv("CT_A", "two", 0) == 0 && !strcmp(getenv("CT_A"), "one"));
	CHECK(setenv("CT_A", "three", 1) == 0 && !strcmp(getenv("CT_A"), "three"));
	for (int i = 0; i < 100; i++) {
		char k[16];
		snprintf(k, sizeof k, "CT_N%d", i);
		CHECK(setenv(k, k, 1) == 0);
	}
	CHECK(!strcmp(getenv("CT_N57"), "CT_N57") && !strcmp(getenv("CITADEL_TEST"), "1"));
	CHECK(unsetenv("CT_A") == 0 && getenv("CT_A") == 0);
	CHECK(unsetenv("CT_A") == 0);
	errno = 0;
	CHECK(setenv("", "x", 1) == -1 && errno == EINVAL);
	CHECK(setenv("A=B", "x", 1) == -1 && errno == EINVAL);
	CHECK(unsetenv("A=B") == -1);
	static char pe[] = "CT_P=put";
	CHECK(putenv(pe) == 0 && getenv("CT_P") == pe + 5);
	pe[5] = 'X'; /* putenv does not copy */
	CHECK(!strcmp(getenv("CT_P"), "Xut"));
	CHECK(setenv("CT_P", "replaced", 1) == 0 && !strcmp(getenv("CT_P"), "replaced"));
	CHECK(secure_getenv("CITADEL_TEST") != 0);
	CHECK(clearenv() == 0 && getenv("CITADEL_TEST") == 0);
	CHECK(setenv("CT_AFTER", "1", 1) == 0 && getenv("CT_AFTER"));

	CHECK(!strcmp(getprogname(), "stdlib"));
	setprogname("/x/y/other");
	CHECK(!strcmp(getprogname(), "other"));
}

static void randomness(void)
{
	srand(42);
	int r1 = rand(), r2 = rand();
	srand(42);
	CHECK(rand() == r1 && rand() == r2 && r1 != r2);
	CHECK(r1 >= 0 && r1 <= RAND_MAX);
	unsigned s = 5, t = 5;
	CHECK(rand_r(&s) == rand_r(&t));
	srandom(7);
	long l1 = random();
	srandom(7);
	CHECK(random() == l1 && l1 >= 0);

	unsigned char b1[64], b2[64];
	arc4random_buf(b1, sizeof b1);
	arc4random_buf(b2, sizeof b2);
	CHECK(memcmp(b1, b2, sizeof b1) != 0);
	/* long request spanning refills and a reseed */
	unsigned char *big = malloc(3000000);
	arc4random_buf(big, 3000000);
	unsigned counts[256] = { 0 };
	for (int i = 0; i < 3000000; i++)
		counts[big[i]]++;
	int ok = 1;
	for (int i = 0; i < 256; i++)
		ok &= counts[i] > 11000 && counts[i] < 12500; /* mean 11719 */
	CHECK(ok);
	free(big);
	unsigned hist[7] = { 0 };
	for (int i = 0; i < 70000; i++) {
		unsigned v = arc4random_uniform(7);
		CHECK(v < 7);
		hist[v < 7 ? v : 0]++;
	}
	ok = 1;
	for (int i = 0; i < 7; i++)
		ok &= hist[i] > 9400 && hist[i] < 10600;
	CHECK(ok);
	CHECK(arc4random_uniform(0) == 0 && arc4random_uniform(1) == 0);
}

static void files(void)
{
	char dir[] = "/tmp/citadel-XXXXXX";
	CHECK(mkdtemp(dir) == dir && strcmp(dir, "/tmp/citadel-XXXXXX"));
	struct stat st;
	CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700);

	char f[64];
	snprintf(f, sizeof f, "%s/fXXXXXX", dir);
	int fd = mkstemp(f);
	CHECK(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
	CHECK(write(fd, "12345", 5) == 5 && fstat(fd, &st) == 0 && st.st_size == 5);
	close(fd);
	char g[64];
	snprintf(g, sizeof g, "%s/gXXXXXX.txt", dir);
	fd = mkstemps(g, 4);
	CHECK(fd >= 0 && !strcmp(g + strlen(g) - 4, ".txt"));
	close(fd);
	char bad[] = "/tmp/noXs";
	errno = 0;
	CHECK(mkstemp(bad) == -1 && errno == EINVAL);

	/* realpath through a symlink chain and dot components */
	char sub[80], l1[80], l2[80], want[4200], got[4200];
	snprintf(sub, sizeof sub, "%s/sub", dir);
	CHECK(mkdir(sub, 0755) == 0);
	snprintf(l1, sizeof l1, "%s/l1", dir);
	snprintf(l2, sizeof l2, "%s/l2", dir);
	CHECK(symlink("sub", l1) == 0);
	CHECK(symlink(l1, l2) == 0);
	CHECK(realpath(dir, want) == want);
	strcat(want, "/sub");
	char q[200];
	snprintf(q, sizeof q, "%s/./l2/../l2/.", dir);
	CHECK(realpath(q, got) == got && !strcmp(got, want));
	char *m = realpath(l2, 0);
	CHECK(m && !strcmp(m, want));
	free(m);
	snprintf(q, sizeof q, "%s/missing", dir);
	errno = 0;
	CHECK(realpath(q, got) == 0 && errno == ENOENT);
	snprintf(q, sizeof q, "%s/f", dir);
	char loop[80];
	snprintf(loop, sizeof loop, "%s/loop", dir);
	CHECK(symlink("loop", loop) == 0);
	errno = 0;
	CHECK(realpath(loop, got) == 0 && errno == ELOOP);
	CHECK(realpath("/", got) && !strcmp(got, "/"));
	CHECK(realpath("/..", got) && !strcmp(got, "/"));

	/* lstat sees the link, stat the target */
	CHECK(lstat(l1, &st) == 0 && S_ISLNK(st.st_mode));
	CHECK(stat(l1, &st) == 0 && S_ISDIR(st.st_mode));
	CHECK(access(f, R_OK | W_OK) == 0 && access(q, F_OK) == -1);
	CHECK(chmod(f, 0400) == 0 && stat(f, &st) == 0 && (st.st_mode & 0777) == 0400);
	/* a read-only file cannot be truncated, except by root */
	if (geteuid() != 0) {
		errno = 0;
		CHECK(truncate(f, 2) == -1 && errno == EACCES);
	}
	CHECK(chmod(f, 0600) == 0);
	CHECK(truncate(f, 2) == 0 && stat(f, &st) == 0 && st.st_size == 2);

	/* clean up */
	unlink(loop);
	unlink(l2);
	unlink(l1);
	rmdir(sub);
	unlink(f);
	unlink(g);
	CHECK(rmdir(dir) == 0);

	void *p = memalign(4096, 10);
	CHECK(p && (uintptr_t)p % 4096 == 0 && malloc_usable_size(p) >= 10);
	free(p);
	p = valloc(1);
	CHECK(p && (uintptr_t)p % 4096 == 0);
	free(p);
	errno = 0;
	CHECK(pvalloc(SIZE_MAX) == 0 && errno == ENOMEM);
}

int main(void)
{
	sorting();
	environment();
	randomness();
	files();
	return t_done();
}
