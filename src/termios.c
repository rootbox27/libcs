/* Terminal attributes. */
#include "internal.h"
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>

int tcgetattr(int fd, struct termios *t)
{
	if (ioctl(fd, TCGETS, t))
		return -1;
	t->__c_ispeed = t->__c_ospeed = t->c_cflag & CBAUD;
	return 0;
}

int tcsetattr(int fd, int act, const struct termios *t)
{
	if ((unsigned)act > TCSAFLUSH) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(fd, TCSETS + act, t);
}

int tcflush(int fd, int q) { return ioctl(fd, TCFLSH, (void *)(long)q); }
int tcdrain(int fd) { return ioctl(fd, TCSBRK, (void *)1L); }
int tcsendbreak(int fd, int dur) { return ioctl(fd, TCSBRK, (void *)0L); }

speed_t cfgetospeed(const struct termios *t) { return t->c_cflag & CBAUD; }
speed_t cfgetispeed(const struct termios *t) { return cfgetospeed(t); }

int cfsetospeed(struct termios *t, speed_t s)
{
	if (s & ~CBAUD) {
		errno = EINVAL;
		return -1;
	}
	t->c_cflag = (t->c_cflag & ~CBAUD) | s;
	t->__c_ospeed = s;
	return 0;
}

int cfsetispeed(struct termios *t, speed_t s)
{
	return s ? cfsetospeed(t, s) : 0;
}

void cfmakeraw(struct termios *t)
{
	t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	t->c_cflag &= ~(CSIZE | PARENB);
	t->c_cflag |= CS8;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}
