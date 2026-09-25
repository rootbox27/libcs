#ifndef _PTY_H
#define _PTY_H
#include <features.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
__BEGIN_DECLS
int openpty(int *, int *, char *, const struct termios *, const struct winsize *);
pid_t forkpty(int *, char *, const struct termios *, const struct winsize *);
int login_tty(int);
__END_DECLS
#endif
