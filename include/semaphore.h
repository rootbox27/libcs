#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H
#include <features.h>
#include <time.h>
#include <fcntl.h>
__BEGIN_DECLS

typedef struct { volatile int __val[8]; } sem_t;
#define SEM_FAILED ((sem_t *)0)
#define SEM_VALUE_MAX 0x7fffffff

int sem_init(sem_t *, int, unsigned);
int sem_destroy(sem_t *);
int sem_wait(sem_t *);
int sem_trywait(sem_t *);
int sem_timedwait(sem_t *__restrict, const struct timespec *__restrict);
int sem_clockwait(sem_t *__restrict, clockid_t, const struct timespec *__restrict);
int sem_post(sem_t *);
int sem_getvalue(sem_t *__restrict, int *__restrict);
sem_t *sem_open(const char *, int, ...);
int sem_close(sem_t *);
int sem_unlink(const char *);

__END_DECLS
#endif
