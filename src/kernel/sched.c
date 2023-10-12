#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>
#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/clock.h>

extern bool panic_flag;
extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);
extern struct proc root_proc;

ListNode runnable;
struct proc* running[NCPU];
struct proc* idle[NCPU];
SpinLock sched_lock;

define_early_init(sched_ds) {
    init_list_node(&runnable);
    memset((void*)running, 0, sizeof(struct proc*) * NCPU);
    memset((void*)idle, 0, sizeof(struct proc*) * NCPU);
    init_spinlock(&sched_lock);
}

struct proc* thisproc()
{
    return running[cpuid()];
}

void init_schinfo(struct schinfo* p)
{
    init_list_node(&(p->runnable_node));
}

void _acquire_sched_lock()
{
    _acquire_spinlock(&sched_lock);
}

void _release_sched_lock()
{
    _release_spinlock(&sched_lock);
}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc* p)
{
    _acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE) {
        _release_sched_lock();
        return false;
    }
    if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        _insert_into_list(&runnable, &(p->schinfo.runnable_node));
    } else {
        PANIC();
    }
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    thisproc()->state = new_state;
    ASSERT(new_state != RUNNING);
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        _detach_from_list(&(thisproc()->schinfo.runnable_node));
    }
}

static struct proc* pick_next()
{
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    struct proc *next = idle[cpuid()];
    _for_in_list(p, &runnable) {
        if (p == &runnable)
            continue;
        next = container_of(p, struct proc, schinfo.runnable_node);
        if (next->state == RUNNABLE)
            return next;
    }
    return next;
}

static void update_this_proc(struct proc* p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    reset_clock(1);
    running[cpuid()] = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        swtch(next->kcontext, &(this->kcontext));
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}