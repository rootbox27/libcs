#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#include <features.h>
__BEGIN_DECLS
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCSBRK 0x5409
#define TCFLSH 0x540B
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD 0x541B
#define TIOCNOTTY 0x5422
#define FIONBIO 0x5421
#define FIOCLEX 0x5451
#define FIONCLEX 0x5450
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
struct winsize;
int ioctl(int, unsigned long, ...);
__END_DECLS
#if 1
#ifndef __DEFINED_winsize
#define __DEFINED_winsize
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#endif
#endif
#endif
