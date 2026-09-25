#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H
#include <features.h>
__BEGIN_DECLS
#define SI_LOAD_SHIFT 16
struct sysinfo {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram, freeram, sharedram, bufferram, totalswap, freeswap;
	unsigned short procs, pad;
	unsigned long totalhigh, freehigh;
	unsigned mem_unit;
	char __reserved[256];
};
int sysinfo(struct sysinfo *);
int get_nprocs(void);
int get_nprocs_conf(void);
long get_phys_pages(void);
long get_avphys_pages(void);
__END_DECLS
#endif
