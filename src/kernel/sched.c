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

Queue runnable;
struct proc* running[NCPU];
struct proc* idle[NCPU];
SpinLock sched_lock;

define_early_init(sched_ds) {
    queue_init(&runnable);
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
    init_list_node(&(p->runable_queue_node));
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
        if (!p->idle) {
            queue_lock(&(runnable));
            queue_push(&(runnable), &(p->schinfo.runable_queue_node));
            queue_unlock(&(runnable));
        }
        _release_sched_lock();
        return true;
    }
    return false;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    /*thisproc()->state = new_state;
    ASSERT(new_state != RUNNING);
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        _detach_from_list(&(thisproc()->schinfo.runable_queue_node));
    }*/
    auto this = thisproc();
    switch (new_state)
    {
    case RUNNABLE:
        if (!this->idle)
        {
            queue_lock(&runnable);
            queue_push(&runnable, &this->schinfo.runable_queue_node);
            queue_unlock(&runnable);
        }
        this->state = new_state;
        running[cpuid()] = NULL;
        return;
    case SLEEPING:
    case ZOMBIE:
        this->state = new_state;
        running[cpuid()] = NULL;
        return;
    case RUNNING:
        return;
    case UNUSED:
    default:
        PANIC();
    }
}

static struct proc* pick_next()
{
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    struct proc *next = idle[cpuid()];
    queue_lock(&runnable);
    if (!queue_empty(&runnable))
    {
        next = container_of(queue_front(&runnable), struct proc, schinfo.runable_queue_node);
        queue_pop(&runnable);
    }
    queue_unlock(&runnable);
    return next;
}

static void update_this_proc(struct proc* p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    ASSERT(running[cpuid()] == NULL);
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
    printk("%d,%d,%d\n", this->pid, next->pid, next->state);
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