#pragma once

#include <lib/defines.h>
#include <aarch64/intrinsic.h>

struct spinlock {
    volatile bool locked;
};

typedef struct spinlock SpinLock;

void init_spinlock(struct spinlock*);
void acquire_spinlock(struct spinlock*);
void release_spinlock(struct spinlock*);
