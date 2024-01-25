#pragma once
#include <lib/spinlock.h>
#include <kernel/param.h>

typedef struct pid_pool PidPool;

struct pid_pool {
	SpinLock lock;
	u8 bm[PID_POOL_SIZE];
	u64 window;
};

int alloc_pid();
void free_pid(int pid);