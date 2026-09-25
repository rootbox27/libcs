/* abort() must terminate with SIGABRT even if SIGABRT is ignored-by-handler. */
#include <stdlib.h>

int main(void)
{
	abort();
}
