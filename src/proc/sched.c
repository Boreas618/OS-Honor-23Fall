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

static struct timer sched_timer[NCPU];

static RBTree rq[NCPU];

define_early_init(rq)
{
	for (int i = 0; i < NCPU; i++)
		rbtree_init(&rq[i]);
}

static bool __cmp_runtime(rb_node n1, rb_node n2)
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

inline struct proc *thisproc()
{
	return cpus[cpuid()].sched.running;
}

bool _activate_proc(struct proc *p, bool onalert)
{
	if ((onalert && p->state == DEEPSLEEPING) || p->state == RUNNING ||
	    p->state == RUNNABLE || p->state == ZOMBIE) {
		return false;
	}

	if (p->state == SLEEPING || p->state == UNUSED ||
	    p->state == DEEPSLEEPING) {
		// Put the process into runnable queue.
		p->state = RUNNABLE;
		if (!p->idle) {
			rbtree_lock(&rq[(p->pid) % NCPU]);
			rbtree_insert(&rq[(p->pid) % NCPU],
				      &(p->schinfo.rq_node), __cmp_runtime);
			rbtree_unlock(&rq[(p->pid) % NCPU]);
		}
		return true;
	}
	return false;
}

static void update_this_state(enum procstate new_state)
{
	ASSERT(new_state != RUNNING);

	if (new_state == RUNNABLE && !thisproc()->idle) {
		rbtree_lock(&rq[cpuid()]);
		rbtree_insert(&rq[cpuid()], &(thisproc()->schinfo.rq_node),
			      __cmp_runtime);
		rbtree_unlock(&rq[cpuid()]);
	}

	if ((new_state == SLEEPING || new_state == ZOMBIE) &&
	    (thisproc()->state == RUNNABLE)) {
		rbtree_lock(&rq[cpuid()]);
		rbtree_erase(&rq[cpuid()], &(thisproc()->schinfo.rq_node));
		rbtree_unlock(&rq[cpuid()]);
	}

	thisproc()->state = new_state;
}

static struct proc *pick_next()
{
	rb_node next_node = rbtree_first(&rq[cpuid()]);
	if (next_node)
		return container_of(container_of(next_node, struct schinfo,
						 rq_node),
				    struct proc, schinfo);
	return cpus[cpuid()].sched.idle;
}

static void __sched_handler(struct timer *t)
{
	t->data = 0;
	thisproc()->schinfo.runtime += SLICE_LEN;
	schedule(RUNNABLE);
}

static void update_this_proc(struct proc *p)
{
	// Reset the current timer
	if (sched_timer[cpuid()].data == 1) {
		cancel_cpu_timer(&sched_timer[cpuid()]);
		sched_timer[cpuid()].data = 0;
	}

	// Set the timer for this p
	cpus[cpuid()].sched.running = p;
	sched_timer[cpuid()].elapse = SLICE_LEN;
	sched_timer[cpuid()].handler = __sched_handler;
	set_cpu_timer(&sched_timer[cpuid()]);
	sched_timer[cpuid()].data = 1;

	// Fetch p from rq and mark it as RUNNING
	p->state = RUNNING;
	if (!p->idle) {
		rbtree_lock(&rq[cpuid()]);
		rbtree_erase(&rq[cpuid()], &(p->schinfo.rq_node));
		rbtree_unlock(&rq[cpuid()]);
	}
}

void schedule(enum procstate new_state)
{
	struct proc *this = thisproc();
	ASSERT(this->state == RUNNING);

	// If the current process is marked as killed, it shouldn't be scheduled as
	// usual.
	if (this->killed && new_state != ZOMBIE) {
		return;
	}

	// Set the state for the old process.
	update_this_state(new_state);

	// Pick the new process.
	struct proc *next = pick_next();

	ASSERT(next->state == RUNNABLE);

	// Update the state of the process.
	update_this_proc(next);

	/**
     * If the picked process is another process,
     * we should perform context switching and attach its virtual memory space
     * before switching.
     */
	if (next != this) {
		set_page_table(next->vmspace.pgtbl);
		swtch(next->kcontext, &(this->kcontext));
	}
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
	set_return_addr(entry);
	return arg;
}