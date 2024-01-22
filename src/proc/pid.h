#pragma once
#include <lib/spinlock.h>

#define PID_POOL_SIZE 1 << 20

typedef struct pidpool PidPool;

struct pidpool {
	SpinLock lock;
	u8 bm[PID_POOL_SIZE];
	u64 window;
};

int alloc_pid();
void free_pid(int pid);