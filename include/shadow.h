#ifndef _SHADOW_H
#define _SHADOW_H
#include <features.h>
#include <stdio.h>
__BEGIN_DECLS
#define SHADOW "/etc/shadow"
struct spwd {
	char *sp_namp, *sp_pwdp;
	long sp_lstchg, sp_min, sp_max, sp_warn, sp_inact, sp_expire;
	unsigned long sp_flag;
};
void setspent(void);
void endspent(void);
struct spwd *getspent(void);
struct spwd *fgetspent(FILE *);
struct spwd *sgetspent(const char *);
int putspent(const struct spwd *, FILE *);
struct spwd *getspnam(const char *);
int getspnam_r(const char *, struct spwd *, char *, size_t, struct spwd **);
int getspent_r(struct spwd *, char *, size_t, struct spwd **);
int fgetspent_r(FILE *, struct spwd *, char *, size_t, struct spwd **);
int sgetspent_r(const char *, struct spwd *, char *, size_t, struct spwd **);
int lckpwdf(void);
int ulckpwdf(void);
__END_DECLS
#endif
