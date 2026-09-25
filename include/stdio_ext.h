#ifndef _STDIO_EXT_H
#define _STDIO_EXT_H
#include <stdio.h>
size_t __fpending(FILE *);
int __freading(FILE *);
int __fwriting(FILE *);
void __fpurge(FILE *);
#endif
