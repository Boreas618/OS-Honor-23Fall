#pragma once

#include <lib/defines.h>
#include <aarch64/intrinsic.h>
#include <lib/checker.h>

struct spinlock {
    volatile bool locked;
};

typedef struct spinlock SpinLock;

__attribute__ ((warn_unused_result)) bool try_acquire_spinlock(SpinLock*);

void acquire_spinlock(SpinLock*);

void release_spinlock(SpinLock*);

void init_spinlock(SpinLock*);
