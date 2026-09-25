/* Internal FILE layout and helpers. Not installed. */
#ifndef CITADEL_STDIO_IMPL_H
#define CITADEL_STDIO_IMPL_H

#include "internal.h"
#include <stdio.h>

#define UNGET 8                 /* bytes reserved before buf for ungetc */

#define F_PERM   0x001          /* static object: never freed */
#define F_NORD   0x004          /* not open for reading */
#define F_NOWR   0x008          /* not open for writing */
#define F_EOF    0x010
#define F_ERR    0x020
#define F_SVB    0x040          /* setvbuf was called */
#define F_APP    0x080          /* append mode */
#define F_TTYCHK 0x100          /* make line buffered on first write if a tty */

typedef size_t (*__f_read_fn)(FILE *, unsigned char *, size_t);
typedef size_t (*__f_write_fn)(FILE *, const unsigned char *, size_t);
typedef off_t (*__f_seek_fn)(FILE *, off_t, int);
typedef int (*__f_close_fn)(FILE *);

/* Buffer state: a stream is in read mode (rpos/rend set), write mode
 * (wpos/wbase/wend set) or neither. buf has UNGET writable bytes before it.
 *
 * The write function writes the buffered bytes [wbase, wpos) followed by
 * its argument, returns how many argument bytes were written, and on
 * success resets the write buffer. On failure it sets F_ERR and clears
 * wpos/wbase/wend. The read function reads into its argument, returning
 * 0 and setting F_EOF or F_ERR at end of file or on error.
 *
 * The operation pointers are stored mangled with the pointer guard;
 * zero selects the plain file-descriptor implementation, so the static
 * standard streams need no run-time initialisation. */
struct __citadel_file {
	unsigned flags;
	unsigned char *rpos, *rend;
	unsigned char *wpos, *wbase, *wend;
	unsigned char *buf;
	size_t buf_size;
	int fd;
	int lbf;                    /* '\n' if line buffered, else -1 */
	uintptr_t read_fn, write_fn, seek_fn, close_fn;
	void *cookie;
	int pipe_pid;               /* popen child, 0 if none */
	volatile int lock;
	int owner, lock_count;
	struct __citadel_file *prev, *next;
	unsigned char sbuf[UNGET + 1]; /* storage when unbuffered */
};

hidden size_t __stdio_read(FILE *, unsigned char *, size_t);
hidden size_t __stdio_write(FILE *, const unsigned char *, size_t);
hidden off_t __stdio_seek(FILE *, off_t, int);
hidden int __stdio_close(FILE *);

static inline uintptr_t __f_fn(void *p)
{
	return p ? __ptr_mangle((uintptr_t)p) : 0;
}
static inline size_t f_read(FILE *f, unsigned char *b, size_t n)
{
	return f->read_fn ? ((__f_read_fn)__ptr_demangle(f->read_fn))(f, b, n) : __stdio_read(f, b, n);
}
static inline size_t f_write(FILE *f, const unsigned char *b, size_t n)
{
	return f->write_fn ? ((__f_write_fn)__ptr_demangle(f->write_fn))(f, b, n) : __stdio_write(f, b, n);
}
static inline off_t f_seek(FILE *f, off_t off, int whence)
{
	return f->seek_fn ? ((__f_seek_fn)__ptr_demangle(f->seek_fn))(f, off, whence) : __stdio_seek(f, off, whence);
}
static inline int f_close(FILE *f)
{
	return f->close_fn ? ((__f_close_fn)__ptr_demangle(f->close_fn))(f) : __stdio_close(f);
}

hidden void __flock(FILE *);
hidden void __funlock(FILE *);
#define FLOCK(f) __flock(f)
#define FUNLOCK(f) __funlock(f)

hidden int __toread(FILE *);
hidden int __towrite(FILE *);
hidden int __uflow(FILE *);
hidden int __overflow(FILE *, int);
hidden size_t __fwritex(const unsigned char *, size_t, FILE *);
hidden FILE *__fdopen_flags(int fd, const char *mode);
hidden FILE *__ofl_add(FILE *);
hidden void __ofl_remove(FILE *);
hidden int __fflush_unlocked(FILE *);
hidden off_t __ftello_unlocked(FILE *);
hidden int __fseeko_unlocked(FILE *, off_t, int);
hidden int __vfprintf_unlocked(FILE *, const char *, va_list);
/* Initialise a stack FILE with the given buffer (size >= UNGET). */
hidden void __file_init(FILE *, int fd, unsigned flags, unsigned char *buf, size_t size);

#define getc_unlocked_(f) ((f)->rpos != (f)->rend ? *(f)->rpos++ : __uflow(f))
#define putc_unlocked_(c, f) \
	(((unsigned char)(c) != (f)->lbf && (f)->wpos != (f)->wend) \
	 ? *(f)->wpos++ = (unsigned char)(c) : __overflow((f), (unsigned char)(c)))

#endif
