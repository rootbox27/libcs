#ifndef _NET_IF_H
#define _NET_IF_H
#include <features.h>
__BEGIN_DECLS

#define IF_NAMESIZE 16

struct if_nameindex { unsigned if_index; char *if_name; };

unsigned if_nametoindex(const char *);
char *if_indextoname(unsigned, char *);
struct if_nameindex *if_nameindex(void);
void if_freenameindex(struct if_nameindex *);

__END_DECLS
#endif
