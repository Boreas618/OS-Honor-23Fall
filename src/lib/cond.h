#include <lib/spinlock.h>
#include <lib/sem.h>

void cond_init (struct semaphore *);
void cond_wait (struct semaphore *, struct spinlock *);
void cond_signal (struct semaphore *);
void cond_broadcast (struct semaphore *);