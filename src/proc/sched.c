#include <aarch64/intrinsic.h>
#include <driver/clock.h>
#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <lib/printk.h>
#include <lib/rbtree.h>
#include <lib/string.h>
#include <proc/proc.h>
#include <proc/sched.h>

#define SLICE_LEN 2

static struct timer sched_timer[NCPU];
static RBTree rq[NCPU];

void swtch(KernelContext *new_ctx, KernelContext **old_ctx);
void trap_return();

define_early_init(sched_helper) 
{
    for (int i = 0; i < NCPU; i++)
        rbtree_init(&rq[i]);
}

static bool 
__cmp_runtime(rb_node n1, rb_node n2) 
{
    if (container_of(n1, struct schinfo, rq_node)->runtime ==
        container_of(n2, struct schinfo, rq_node)->runtime)
        return container_of(container_of(n1, struct schinfo, rq_node),
                            struct proc, schinfo)
                   ->pid <
               container_of(container_of(n2, struct schinfo, rq_node),
                            struct proc, schinfo)
                   ->pid;
    return container_of(n1, struct schinfo, rq_node)->runtime <
           container_of(n2, struct schinfo, rq_node)->runtime;
}

inline Proc *thisproc() { return cpus[cpuid()].sched.running; }

bool 
_activate_proc(Proc *p, bool onalert) 
{
    if ((onalert && p->state == DEEPSLEEPING) || p->state == RUNNING ||
        p->state == RUNNABLE || p->state == ZOMBIE)
        return false;
    if (p->state == SLEEPING || p->state == UNUSED ||
        p->state == DEEPSLEEPING) {
        p->state = RUNNABLE;
        if (!p->idle)
            rbtree_insert(&rq[(p->pid) % NCPU], &(p->schinfo.rq_node),
                          __cmp_runtime);
        return true;
    }
    return false;
}

static void 
update_this_state(enum procstate new_state) 
{
    ASSERT(new_state != RUNNING);

    if (new_state == RUNNABLE && !thisproc()->idle)
        rbtree_insert(&rq[cpuid()], &(thisproc()->schinfo.rq_node),
                      __cmp_runtime);

    if ((new_state == SLEEPING || new_state == ZOMBIE) &&
        (thisproc()->state == RUNNABLE))
        rbtree_erase(&rq[cpuid()], &(thisproc()->schinfo.rq_node));

    thisproc()->state = new_state;
}

static Proc *
pick_next() 
{
    rb_node next_node = rbtree_first(&rq[cpuid()]);
    if (next_node)
        return container_of(container_of(next_node, struct schinfo, rq_node),
                            struct proc, schinfo);
    return cpus[cpuid()].sched.idle;
}

static void 
_sched_handler(struct timer *t) 
{
    t->data--;
    // @TODO overflow of runtime
    thisproc()->schinfo.runtime += SLICE_LEN;
    _sched(RUNNABLE);
}

static void 
update_this_proc(Proc *p) 
{
    if (sched_timer[cpuid()].data > 0) {
        cancel_cpu_timer(&sched_timer[cpuid()]);
        sched_timer[cpuid()].data--;
    }
    cpus[cpuid()].sched.running = p;
    sched_timer[cpuid()].elapse = SLICE_LEN;
    sched_timer[cpuid()].handler = _sched_handler;
    set_cpu_timer(&sched_timer[cpuid()]);
    sched_timer[cpuid()].data++;
    ASSERT(p->state == RUNNABLE);
    p->state = RUNNING;
    if (!p->idle)
        rbtree_erase(&rq[cpuid()], &(p->schinfo.rq_node));
}

static void 
schedule(enum procstate new_state) 
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    if (this->killed && new_state != ZOMBIE)
        return;
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    if (next != this) {
        attach_vmspace(&next->vmspace);
        swtch(next->kcontext, &(this->kcontext));
    }
}

__attribute__((weak, alias("schedule"))) void _sched(enum procstate new_state);

u64 
proc_entry (void (*entry)(u64), u64 arg) 
{
    set_return_addr(entry);
    return arg;
}