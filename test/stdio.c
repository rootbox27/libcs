#include "harness.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char path[64];

static void make_path(void)
{
	/* unique per process so the three link modes can run in parallel */
	snprintf(path, sizeof path, "/tmp/citadel-stdio-%d", getpid());
}

static void write_read(void)
{
	FILE *f = fopen(path, "w");
	CHECK(f != 0);
	if (!f)
		return;
	CHECK(fputs("line one\n", f) >= 0);
	CHECK(fprintf(f, "line %s %d\n", "two", 2) == 11);
	CHECK(fputc('x', f) == 'x');
	CHECK(fwrite("yz\nlast", 1, 7, f) == 7);
	CHECK(ftell(f) == 28);
	CHECK(fclose(f) == 0);

	f = fopen(path, "r");
	CHECK(f != 0);
	if (!f)
		return;
	char b[64];
	CHECK(fgets(b, sizeof b, f) == b && !strcmp(b, "line one\n"));
	CHECK(fgetc(f) == 'l');
	CHECK(ungetc('L', f) == 'L');
	CHECK(fgets(b, sizeof b, f) == b && !strcmp(b, "Line two 2\n"));
	/* fgets stops at the buffer size */
	CHECK(fgets(b, 3, f) == b && !strcmp(b, "xy"));
	CHECK(fgets(b, sizeof b, f) == b && !strcmp(b, "z\n"));
	CHECK(fgets(b, sizeof b, f) == b && !strcmp(b, "last"));
	CHECK(fgets(b, sizeof b, f) == 0);
	CHECK(feof(f) && !ferror(f));
	CHECK(fgetc(f) == EOF);
	/* seek clears EOF */
	CHECK(fseek(f, 5, SEEK_SET) == 0 && !feof(f));
	CHECK(ftell(f) == 5);
	CHECK(fread(b, 1, 3, f) == 3 && !memcmp(b, "one", 3));
	CHECK(ftell(f) == 8);
	CHECK(fseek(f, -4, SEEK_END) == 0 && fgetc(f) == 'l');
	CHECK(fseek(f, -5, SEEK_CUR) == 0 && fgetc(f) == 'x');
	rewind(f);
	CHECK(ftell(f) == 0);
	fpos_t pos;
	CHECK(fgetc(f) == 'l' && fgetpos(f, &pos) == 0);
	CHECK(fgetc(f) == 'i' && fsetpos(f, &pos) == 0 && fgetc(f) == 'i');
	/* fread of more than is available */
	rewind(f);
	CHECK(fread(b, 1, sizeof b, f) == 28 && feof(f));
	/* fread counts whole members only */
	rewind(f);
	CHECK(fread(b, 10, 5, f) == 2);
	/* ungetc then fread */
	rewind(f);
	CHECK(ungetc('Q', f) == 'Q');
	CHECK(fread(b, 1, 4, f) == 4 && !memcmp(b, "Qlin", 4));
	/* writing to a read-only stream fails and sets the error flag */
	CHECK(fputc('a', f) == EOF && ferror(f));
	clearerr(f);
	CHECK(!ferror(f) && !feof(f));
	CHECK(fileno(f) >= 3);
	CHECK(fclose(f) == 0);
}

static void lines(void)
{
	FILE *f = fopen(path, "w+");
	CHECK(f != 0);
	if (!f)
		return;
	for (int i = 0; i < 100; i++)
		fprintf(f, "%d:%0*d\n", i, i * 50, 0);
	fputs("no newline", f);
	rewind(f);
	char *line = 0;
	size_t cap = 0;
	ssize_t n;
	int i = 0, ok = 1;
	while ((n = getline(&line, &cap, f)) > 0) {
		if (i < 100) {
			char want[16];
			int k = snprintf(want, sizeof want, "%d:", i);
			ok &= n == k + (i ? i * 50 : 1) + 1 && !strncmp(line, want, (size_t)k) && line[n - 1] == '\n' && line[n] == 0;
		} else {
			ok &= !strcmp(line, "no newline");
		}
		i++;
	}
	CHECK(ok && i == 101);
	CHECK(n == -1 && feof(f));
	CHECK(cap > 100 * 50);
	rewind(f);
	CHECK(getdelim(&line, &cap, ':', f) == 2 && !strcmp(line, "0:"));
	free(line);
	errno = 0;
	CHECK(getline(0, &cap, f) == -1 && errno == EINVAL);
	fclose(f);
}

static void modes(void)
{
	FILE *f = fopen(path, "w");
	fputs("abc", f);
	fclose(f);

	/* append always writes at the end, even after a seek */
	f = fopen(path, "a+");
	CHECK(f != 0);
	if (!f)
		return;
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	CHECK(fgetc(f) == 'a');
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	fputs("de", f);
	CHECK(ftell(f) == 5);
	rewind(f);
	char b[16] = { 0 };
	CHECK(fread(b, 1, sizeof b - 1, f) == 5 && !strcmp(b, "abcde"));
	fclose(f);

	/* r+: read, seek, overwrite, read back */
	f = fopen(path, "r+");
	CHECK(fgetc(f) == 'a');
	CHECK(fseek(f, 0, SEEK_CUR) == 0);
	CHECK(fputc('B', f) == 'B');
	CHECK(fflush(f) == 0);
	rewind(f);
	memset(b, 0, sizeof b);
	CHECK(fread(b, 1, 5, f) == 5 && !strcmp(b, "aBcde"));
	fclose(f);

	/* x: exclusive create */
	errno = 0;
	CHECK(fopen(path, "wx") == 0 && errno == EEXIST);
	/* bad mode */
	errno = 0;
	CHECK(fopen(path, "q") == 0 && errno == EINVAL);
	/* e: close-on-exec */
	f = fopen(path, "re");
	CHECK(f && (fcntl(fileno(f), F_GETFD) & FD_CLOEXEC));
	fclose(f);
	errno = 0;
	CHECK(fopen("/nonexistent/dir/x", "r") == 0 && errno == ENOENT);

	/* fdopen shares the descriptor */
	int fd = open(path, O_RDONLY);
	f = fdopen(fd, "r");
	CHECK(f && fileno(f) == fd && fgetc(f) == 'a');
	fclose(f);
	CHECK(close(fd) == -1 && errno == EBADF);
}

static void buffering(void)
{
	int p[2];
	CHECK(pipe(p) == 0);
	FILE *f = fdopen(p[1], "w");
	char b[32];
	/* fully buffered: nothing reaches the pipe until fflush */
	CHECK(setvbuf(f, 0, _IOFBF, 0) == 0);
	fputs("abc", f);
	CHECK(__fpending(f) == 3);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	CHECK(read(p[0], b, sizeof b) == -1 && errno == EAGAIN);
	fflush(f);
	CHECK(read(p[0], b, sizeof b) == 3);
	/* line buffered: flushed at newline */
	CHECK(setvbuf(f, 0, _IOLBF, 0) == 0);
	fputs("x\ny", f);
	CHECK(read(p[0], b, sizeof b) == 2 && __fpending(f) == 1);
	fflush(f);
	CHECK(read(p[0], b, sizeof b) == 1);
	/* unbuffered: each call writes; printf in one write */
	CHECK(setvbuf(f, 0, _IONBF, 0) == 0);
	fputc('q', f);
	CHECK(read(p[0], b, sizeof b) == 1);
	fprintf(f, "%d-%d-%d", 1, 2, 3);
	CHECK(read(p[0], b, sizeof b) == 5 && !memcmp(b, "1-2-3", 5));
	/* user-supplied buffer */
	static char ubuf[64];
	CHECK(setvbuf(f, ubuf, _IOFBF, sizeof ubuf) == 0);
	fputs("12345", f);
	CHECK(read(p[0], b, sizeof b) == -1);
	fflush(f);
	CHECK(read(p[0], b, sizeof b) == 5);
	CHECK(setvbuf(f, 0, 99, 0) != 0);
	fclose(f);
	close(p[0]);

	/* write error: reported by fflush/fclose, sticky in ferror */
	CHECK(pipe(p) == 0);
	close(p[0]);
	f = fdopen(p[1], "w");
	/* SIGPIPE would kill us; there is no signal() yet, so block it. */
	unsigned long set = 1UL << (13 - 1);
	register long r10 __asm__("r10") = 8;
	long rr;
	__asm__ __volatile__("syscall" : "=a"(rr) : "a"(14L), "D"(0L), "S"(&set), "d"(0L), "r"(r10) : "rcx", "r11", "memory");
	fputs("data", f);
	errno = 0;
	CHECK(fflush(f) == EOF && ferror(f) && errno == EPIPE);
	fclose(f);
}

static void memstreams(void)
{
	char b[16];
	FILE *f = fmemopen(b, sizeof b, "w");
	CHECK(f != 0);
	CHECK(fprintf(f, "%d+%d", 12, 34) == 5);
	fflush(f);
	CHECK(!strcmp(b, "12+34"));
	/* writing past the end fails (once the buffer is flushed) */
	fputs("0123456789abcdef", f);
	CHECK(fflush(f) == EOF && ferror(f) && errno == ENOSPC);
	CHECK(!strncmp(b, "12+340123456789a", 16));
	fclose(f);

	strcpy(b, "hello world");
	f = fmemopen(b, strlen(b), "r");
	char w[8];
	CHECK(fread(w, 1, 5, f) == 5 && !memcmp(w, "hello", 5));
	CHECK(fseek(f, -5, SEEK_END) == 0 && fgetc(f) == 'w');
	CHECK(fseek(f, 100, SEEK_SET) == -1 && errno == EINVAL);
	fclose(f);

	strcpy(b, "ab");
	f = fmemopen(b, sizeof b, "a");
	fputs("cd", f);
	fclose(f);
	CHECK(!strcmp(b, "abcd"));

	f = fmemopen(0, 32, "w+");
	CHECK(f != 0);
	fputs("scratch", f);
	rewind(f);
	CHECK(fgets(w, sizeof w, f) && !strcmp(w, "scratch"));
	fclose(f);
	errno = 0;
	CHECK(fmemopen(b, 0, "r") == 0 && errno == EINVAL);

	char *ms = 0;
	size_t len = 99;
	f = open_memstream(&ms, &len);
	CHECK(f && ms && len == 0);
	for (int i = 0; i < 2000; i++)
		fprintf(f, "%04d", i);
	fflush(f);
	CHECK(len == 8000 && !strncmp(ms, "00000001", 8) && !strcmp(ms + 7996, "1999"));
	fseek(f, 4, SEEK_SET);
	fputs("XX", f);
	fflush(f);
	CHECK(len == 6 && !strncmp(ms, "0000XX01", 8) && strlen(ms) == 8000);
	fclose(f);
	free(ms);
}

static void misc(void)
{
	FILE *t = tmpfile();
	CHECK(t != 0);
	if (t) {
		fputs("tmp", t);
		rewind(t);
		CHECK(fgetc(t) == 't');
		fclose(t);
	}
	CHECK(remove(path) == 0);
	CHECK(remove(path) == -1 && errno == ENOENT);
	CHECK(!strcmp(strerror(EINVAL), "Invalid argument"));
	CHECK(!strcmp(strerror(12345), "Unknown error"));
	CHECK(!strcmp(strerrorname_np(ENOENT), "ENOENT"));
	char eb[64];
	CHECK(strerror_r(EPERM, eb, sizeof eb) == 0 && !strcmp(eb, "Operation not permitted"));
	CHECK(strerror_r(EPERM, eb, 5) == ERANGE && !strcmp(eb, "Oper"));
	CHECK(strerror_r(-7, eb, sizeof eb) == EINVAL && !strcmp(eb, "Unknown error -7"));
	CHECK(!strcmp(strsignal(11), "Segmentation fault"));
}

int main(void)
{
	make_path();
	write_read();
	lines();
	modes();
	buffering();
	memstreams();
	misc();
	return t_done();
}
