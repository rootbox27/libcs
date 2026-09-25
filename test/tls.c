/* TLS with a small segment alignment: the linker places variables at
 * tp - round_up(memsz, p_align), so startup must use that exact offset. */
#include "harness.h"

static __thread int a = 0x11223344;
static __thread char b = 'q';
static __thread short c;

int main(void)
{
	CHECK(a == 0x11223344);
	CHECK(b == 'q');
	CHECK(c == 0);
	c = 7;
	a++;
	CHECK(a == 0x11223345 && b == 'q' && c == 7);
	return t_done();
}
