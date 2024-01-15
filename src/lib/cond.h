#include <lib/sem.h>

struct condition {
    struct semaphore sem;
};

void cond_init (struct condition *);
bool cond_wait (struct condition *, struct spinlock *);
void cond_signal (struct condition *);
void cond_broadcast (struct condition *);