#ifndef _CRYPT_H
#define _CRYPT_H
#include <features.h>
__BEGIN_DECLS
struct crypt_data {
	int initialized;
	char output[256];
};
char *crypt(const char *, const char *);
char *crypt_r(const char *, const char *, struct crypt_data *);
__END_DECLS
#endif
