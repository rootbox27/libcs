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
#define SIOCATMARK 0x8905
#define SIOCGSTAMP 0x8906
#define SIOCGIFNAME 0x8910
#define SIOCGIFCONF 0x8912
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCSIFADDR 0x8916
#define SIOCGIFDSTADDR 0x8917
#define SIOCSIFDSTADDR 0x8918
#define SIOCGIFBRDADDR 0x8919
#define SIOCSIFBRDADDR 0x891a
#define SIOCGIFNETMASK 0x891b
#define SIOCSIFNETMASK 0x891c
#define SIOCGIFMETRIC 0x891d
#define SIOCGIFMTU 0x8921
#define SIOCSIFMTU 0x8922
#define SIOCSIFNAME 0x8923
#define SIOCSIFHWADDR 0x8924
#define SIOCGIFHWADDR 0x8927
#define SIOCGIFINDEX 0x8933
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
