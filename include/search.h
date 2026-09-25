#ifndef _SEARCH_H
#define _SEARCH_H
#include <features.h>
#include <stddef.h>
__BEGIN_DECLS

typedef enum { FIND, ENTER } ACTION;
typedef enum { preorder, postorder, endorder, leaf } VISIT;

typedef struct entry {
	char *key;
	void *data;
} ENTRY;

int hcreate(size_t);
void hdestroy(void);
ENTRY *hsearch(ENTRY, ACTION);

struct hsearch_data {
	struct __htab *__tab;
	unsigned __unused1, __unused2;
};
int hcreate_r(size_t, struct hsearch_data *);
void hdestroy_r(struct hsearch_data *);
int hsearch_r(ENTRY, ACTION, ENTRY **, struct hsearch_data *);

void *tsearch(const void *, void **, int (*)(const void *, const void *));
void *tfind(const void *, void *const *, int (*)(const void *, const void *));
void *tdelete(const void *__restrict, void **__restrict, int (*)(const void *, const void *));
void twalk(const void *, void (*)(const void *, VISIT, int));
void tdestroy(void *, void (*)(void *));

void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *));
void *lfind(const void *, const void *, size_t *, size_t, int (*)(const void *, const void *));

void insque(void *, void *);
void remque(void *);

__END_DECLS
#endif
