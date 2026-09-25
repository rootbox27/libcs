/* siglongjmp: restore the signal mask saved by sigsetjmp, then jump.
 * (Kept out of setjmp.S so the archive has no two members named setjmp.o.) */
#include "internal.h"
#include <setjmp.h>
#include <signal.h>

void siglongjmp(sigjmp_buf buf, int val)
{
	if (buf->__mask_was_saved)
		__sys(SYS_rt_sigprocmask, SIG_SETMASK, &buf->__ss, 0, 8);
	longjmp(buf, val);
}
