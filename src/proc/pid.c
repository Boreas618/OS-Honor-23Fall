#include <kernel/init.h>
#include <proc/pid.h>
#include <lib/bitmap.h>

static PidPool pool;

define_early_init(pid)
{
    init_spinlock(&pool.lock);
    pool.window = 0;
}

int 
alloc_pid() 
{
    acquire_spinlock(&pool.lock);
    u64 old_pid_window = pool.window;

    while (pool.bm[pool.window] == 0xFF) {
        pool.window++;
        if (pool.window == PID_POOL_SIZE)
            pool.window = 0;
        if (pool.window == old_pid_window) {
            release_spinlock(&pool.lock);
            return -1;
        }
    }

    for (int i = 0; i < 8; i++)
        if ((pool.bm[pool.window] ^ (1 << i)) & (1 << i)) {
            pool.bm[pool.window] |= (1 << i);
            int pid = (int)(pool.window * 8 + i);
            release_spinlock(&pool.lock);
            return pid;
        }
    release_spinlock(&pool.lock);
    return -1;
}

void 
free_pid(int pid) 
{
    acquire_spinlock(&pool.lock);
    u32 index = (u32)(pid / 8);
    u8 offset = (u8)(pid % 8);
    pool.bm[index] &= ~(1 << (u8)offset);
    release_spinlock(&pool.lock);
}