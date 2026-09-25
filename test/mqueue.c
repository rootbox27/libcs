/* <mqueue.h> */
#include <mqueue.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "harness.h"

int main(void)
{
	struct mq_attr a = { 0, 4, 32, 0, { 0 } };
	char name[] = "/citadel-mq-test";
	mq_unlink(name);
	mqd_t q = mq_open(name, O_RDWR | O_CREAT | O_EXCL, 0600, &a);
	if (q < 0 && (errno == ENOSYS || errno == EACCES || errno == ENOENT)) {
		t_puts("no message queues here: skipping\n");
		return t_done();
	}
	CHECK(q >= 0);
	CHECK(mq_send(q, "low", 3, 1) == 0 && mq_send(q, "high", 4, 9) == 0);
	struct mq_attr cur;
	CHECK(mq_getattr(q, &cur) == 0 && cur.mq_curmsgs == 2 && cur.mq_msgsize == 32);
	char buf[32];
	unsigned prio;
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 4 && prio == 9 && !memcmp(buf, "high", 4));
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == 3 && prio == 1);
	struct mq_attr nb = { O_NONBLOCK, 0, 0, 0, { 0 } };
	CHECK(mq_setattr(q, &nb, 0) == 0);
	CHECK(mq_receive(q, buf, sizeof buf, &prio) == -1 && errno == EAGAIN);
	CHECK(mq_receive(q, buf, 8, &prio) == -1 && errno == EMSGSIZE);
	CHECK(mq_close(q) == 0 && mq_unlink(name) == 0);
	CHECK(mq_open(name, O_RDONLY) == -1 && errno == ENOENT);
	return t_done();
}
