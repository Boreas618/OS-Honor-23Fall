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

void trap_return();

ListNode runnable;
SpinLock sched_lock;

define_early_init(sched_helper) {
    init_list_node(&runnable);
    init_spinlock(&sched_lock);
}

struct proc* thisproc()
{
    return cpus[cpuid()].sched.running;
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

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEPING, do nothing if onalert or activate it if else, and return the corresponding value.
    _acquire_sched_lock();
    if (p->state == RUNNING || p->state == RUNNABLE || p->state == ZOMBIE) {
        _release_sched_lock();
        return false;
    }
    if (p->state == SLEEPING || p->state == UNUSED) {
        p->state = RUNNABLE;
        if (!p->idle)
            _insert_into_list(&runnable, &p->schinfo.runnable_node);
        _release_sched_lock();
        return true;
    }
    _release_sched_lock();
}

static void update_this_state(enum procstate new_state)
{
    thisproc()->state = new_state;
    ASSERT(new_state != RUNNING);

    if (new_state == RUNNABLE && !thisproc()->idle) {
        _detach_from_list(&(thisproc()->schinfo.runnable_node));
        _insert_into_list(runnable.prev, &(thisproc()->schinfo.runnable_node));
    }
    
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        _detach_from_list(&(thisproc()->schinfo.runnable_node));
    }
}

static struct proc* pick_next() {
  ListNode* prunable = runnable.next;

  while (prunable != &runnable) {
    struct proc* candidate =
        container_of(prunable, struct proc, schinfo.runnable_node);
    if (candidate->state == RUNNABLE) {
      return candidate;
    }
    prunable = prunable->next;
  }
  return cpus[cpuid()].sched.idle;
}

static void update_this_proc(struct proc* p)
{
    reset_clock(20);
    cpus[cpuid()].sched.running = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if (this->killed && new_state != ZOMBIE) {
        _release_sched_lock();
        return;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        attach_pgdir(&(next->pgdir));
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