/* Overflowing a thread's stack must hit its guard page. */
#include <pthread.h>
#include <string.h>

static int deep(int n);
/* called through a volatile pointer so the recursion is neither
 * optimised away nor rejected as infinite */
static int (*volatile recurse)(int) = deep;

static int deep(int n)
{
	volatile char pad[1024];
	memset((char *)pad, n, sizeof pad);
	return recurse(n + 1) + pad[3];
}

static void *run(void *a)
{
	return (void *)(long)recurse(0);
}

int main(void)
{
	pthread_attr_t a;
	pthread_t t;
	pthread_attr_init(&a);
	pthread_attr_setstacksize(&a, 1 << 16);
	pthread_create(&t, &a, run, 0);
	pthread_join(t, 0);
	return 0;
}
