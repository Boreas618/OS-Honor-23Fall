#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/cpu.h>
#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <driver/clock.h>
#include <common/rbtree.h>

/* The length should be between 10 - 100. Otherwise there would be timer issues.*/
#define SLICE_LEN 10

extern bool panic_flag;
extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);
extern struct proc root_proc;
extern struct proc* running[];

static struct timer sched_timer[NCPU];

void trap_return();

static RBTree rq;

SpinLock sched_lock;

define_early_init(sched_helper) {
    rbtree_init(&rq);
    init_spinlock(&sched_lock);
}

static bool _cmp_runtime(rb_node n1, rb_node n2) {
    if (container_of(n1, struct schinfo, rq_node)->runtime ==
           container_of(n2, struct schinfo, rq_node)->runtime)
           return container_of(container_of(n1, struct schinfo, rq_node), struct proc, schinfo)->pid <
           container_of(container_of(n2, struct schinfo, rq_node), struct proc, schinfo)->pid;
    return container_of(n1, struct schinfo, rq_node)->runtime <
           container_of(n2, struct schinfo, rq_node)->runtime;
}

inline struct proc* thisproc() {
    return cpus[cpuid()].sched.running;
}

inline void _acquire_sched_lock() {
    _acquire_spinlock(&sched_lock);
}

inline void _release_sched_lock() {
    _release_spinlock(&sched_lock);
}

bool _activate_proc(struct proc* p, bool onalert) {
    _acquire_sched_lock();
    if ((onalert && p->state == DEEPSLEEPING) || p->state == RUNNING || p->state == RUNNABLE || p->state == ZOMBIE) {
        _release_sched_lock();
        return false;
    }
    if (p->state == SLEEPING || p->state == UNUSED || p->state == DEEPSLEEPING) {
        p->state = RUNNABLE;
        if (!p->idle) rbtree_insert(&rq, &(p->schinfo.rq_node), _cmp_runtime);
        _release_sched_lock();
        return true;
    }
    _release_sched_lock();
    return false;
}

static void update_this_state(enum procstate new_state) {
    ASSERT(new_state != RUNNING);

    if (new_state == RUNNABLE && !thisproc()->idle)
        rbtree_insert(&rq, &(thisproc()->schinfo.rq_node), _cmp_runtime);
    
    if ((new_state == SLEEPING || new_state == ZOMBIE) && (thisproc()->state == RUNNABLE))
        rbtree_erase(&rq, &(thisproc()->schinfo.rq_node));

    thisproc()->state = new_state;
}

static struct proc* pick_next() {
    rb_node next_node = rbtree_first(&rq);
    if (next_node)
        return container_of(container_of(next_node, struct schinfo, rq_node), struct proc, schinfo);
    return cpus[cpuid()].sched.idle;
}

static void _sched_handler(struct timer* t) {
    _acquire_sched_lock();
    t->data--;
    // @TODO overflow of runtime
    thisproc()->schinfo.runtime += SLICE_LEN;
    _sched(RUNNABLE);
}

static void update_this_proc(struct proc* p) {
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
    if (!p->idle) rbtree_erase(&rq, &(p->schinfo.rq_node));
}

static void schedule(enum procstate new_state) {
    auto this = thisproc();
    ASSERT (this->state == RUNNING);
    if (this->killed && new_state != ZOMBIE) {
        _release_sched_lock();
        return;
    }
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    if (next != this) {
        attach_pgdir(&(next->pgdir));
        swtch(next->kcontext, &(this->kcontext));
    }
    _release_sched_lock();
}

__attribute__((weak, alias("schedule"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg) {
    set_return_addr(entry);
    _release_sched_lock();
    return arg;
}