#ifndef _TERMIOS_H
#define _TERMIOS_H
#include <features.h>
#include <bits/alltypes.h>
__BEGIN_DECLS
typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;
#define NCCS 32
struct termios {
	tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t __c_ispeed, __c_ospeed;
};
#ifndef __DEFINED_winsize
#define __DEFINED_winsize
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#endif
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VWERASE 14
#define VLNEXT 15
#define IGNBRK 0000001
#define BRKINT 0000002
#define IGNPAR 0000004
#define PARMRK 0000010
#define INPCK 0000020
#define ISTRIP 0000040
#define INLCR 0000100
#define IGNCR 0000200
#define ICRNL 0000400
#define IXON 0002000
#define IXANY 0004000
#define IXOFF 0010000
#define IMAXBEL 0020000
#define IUTF8 0040000
#define OPOST 0000001
#define ONLCR 0000004
#define CSIZE 0000060
#define CS5 0000000
#define CS6 0000020
#define CS7 0000040
#define CS8 0000060
#define CSTOPB 0000100
#define CREAD 0000200
#define PARENB 0000400
#define PARODD 0001000
#define HUPCL 0002000
#define CLOCAL 0004000
#define ISIG 0000001
#define ICANON 0000002
#define ECHO 0000010
#define ECHOE 0000020
#define ECHOK 0000040
#define ECHONL 0000100
#define NOFLSH 0000200
#define TOSTOP 0000400
#define ECHOCTL 0001000
#define ECHOKE 0004000
#define IEXTEN 0100000
#define B0 0000000
#define B9600 0000015
#define B19200 0000016
#define B38400 0000017
#define B57600 0010001
#define B115200 0010002
#define CBAUD 0010017
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
int tcgetattr(int, struct termios *);
int tcsetattr(int, int, const struct termios *);
int tcflush(int, int);
int tcdrain(int);
int tcsendbreak(int, int);
speed_t cfgetospeed(const struct termios *);
speed_t cfgetispeed(const struct termios *);
int cfsetospeed(struct termios *, speed_t);
int cfsetispeed(struct termios *, speed_t);
void cfmakeraw(struct termios *);
__END_DECLS
#endif
