#ifndef _WORDEXP_H
#define _WORDEXP_H
#include <features.h>
#define __need_size_t
#include <stddef.h>
__BEGIN_DECLS
typedef struct {
	size_t we_wordc;
	char **we_wordv;
	size_t we_offs;
} wordexp_t;

#define WRDE_DOOFFS  (1 << 0)
#define WRDE_APPEND  (1 << 1)
#define WRDE_NOCMD   (1 << 2)
#define WRDE_REUSE   (1 << 3)
#define WRDE_SHOWERR (1 << 4)
#define WRDE_UNDEF   (1 << 5)

#define WRDE_NOSYS   (-1)
#define WRDE_NOSPACE 1
#define WRDE_BADCHAR 2
#define WRDE_BADVAL  3
#define WRDE_CMDSUB  4
#define WRDE_SYNTAX  5

int wordexp(const char *__restrict, wordexp_t *__restrict, int);
void wordfree(wordexp_t *);
__END_DECLS
#endif
